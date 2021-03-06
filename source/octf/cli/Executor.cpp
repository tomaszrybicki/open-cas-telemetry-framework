/*
 * Copyright(c) 2012-2018 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <octf/cli/Executor.h>

#include <google/protobuf/dynamic_message.h>
#include <exception>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <octf/cli/CLIList.h>
#include <octf/cli/CLIProperties.h>
#include <octf/cli/CLIUtils.h>
#include <octf/cli/CommandSet.h>
#include <octf/cli/cmd/CmdVersion.h>
#include <octf/cli/cmd/CommandProtobuf.h>
#include <octf/cli/cmd/CommandProtobufLocal.h>
#include <octf/node/INode.h>
#include <octf/node/NodeClient.h>
#include <octf/proto/InterfaceCLI.pb.h>
#include <octf/utils/Exception.h>
#include <octf/utils/Log.h>
#include <octf/utils/ModulesDiscover.h>
#include <octf/utils/OptionsValidation.h>

using std::endl;
using std::make_shared;
using std::shared_ptr;
using std::string;
using std::stringstream;

namespace octf {

Executor::Executor()
        : m_localCmdSet()
        , m_moduleCmdSet()
        , m_modules()
        , m_module()
        , m_progress(0.0)
        , m_nodePlugin() {
    // Get a list of available modules
    getModules();
}

void Executor::addLocalCommand(shared_ptr<ICommand> cmd) {
    m_localCmdSet.addCmd(cmd);
}

void Executor::loadModuleCommandSet() {
    if (m_module.isLocal()) {
        // Module is local, set the appropriate command set
        m_moduleCmdSet = m_localModules[m_module.getLongKey()];

    } else {
        // Get description of this module's command set
        Call<proto::Void, proto::CliCommandSet> call(m_nodePlugin.get());
        m_nodePlugin->getCliInterface()->getCliCommandSetDescription(
                &call, call.getInput().get(), call.getOutput().get(), &call);
        call.wait();

        if (call.Failed()) {
            throw InvalidParameterException("Cannot get command list, error: " +
                                            call.ErrorText());
        }

        auto cmdSetDesc = call.getOutput();
        if (cliUtils::isCommandSetValid(*cmdSetDesc)) {
            int commandCount = cmdSetDesc->command_size();
            for (int i = 0; i < commandCount; i++) {
                const proto::CliCommand &cmdDesc = cmdSetDesc->command(i);
                auto cmd = make_shared<CommandProtobuf>(cmdDesc);
                m_moduleCmdSet.addCmd(cmd);
            }
        }
    }
}

void Executor::printMainHelp(std::stringstream &ss) {
    CLIUtils::printUsage(ss, nullptr, false, m_modules.size());

    if (m_modules.size()) {
        ss << endl << "Available modules: " << endl;
        for (auto module : m_modules) {
            CLIUtils::printModuleHelp(ss, &module.second, true);
        }
    }

    CLIUtils::printCmdSetHelp(ss, m_localCmdSet);

    return;
}

void Executor::getModules() {
    ModulesDiscover discover;
    NodesIdList nodes;

    // Get a list of modules which sockets were detected
    discover.getModulesList(nodes);

    // Add modules
    for (auto node : nodes) {
        Module newModule;
        newModule.setLongKey(node.getId());
        m_modules[node.getId()] = newModule;
    }
}

shared_ptr<ICommand> Executor::validateCommand(CLIList &cliList) {
    CLIElement element = cliList.nextElement();
    string key = element.getValidKeyName();
    if (key.empty()) {
        throw InvalidParameterException("Invalid command format.");
    }
    string cmd;
    bool localCommand;

    if (isModuleExistent(key)) {
        // Dealing with module
        setModule(key);
        localCommand = false;

        if (cliList.hasNext()) {
            element = cliList.nextElement();
            cmd = element.getValidKeyName();
            if (cmd.empty()) {
                return nullptr;
            }
        } else {
            // No command specified
            return nullptr;
        }

    } else {
        // Dealing with local command
        cmd = key;
        localCommand = true;
    }

    // Look for command in local or module command set
    shared_ptr<ICommand> commandToExecute;
    if (localCommand) {
        // Local command
        commandToExecute = m_localCmdSet.getCmd(cmd);

    } else {
        // Module command
        if (m_moduleCmdSet.hasCmd(cmd)) {
            // Module command set already loaded
            commandToExecute = m_moduleCmdSet.getCmd(cmd);
        } else {
            // Module command set not loaded or command not existent
            commandToExecute = getCommandFromModule(cmd);
        }
    }

    return commandToExecute;
}

shared_ptr<ICommand> Executor::getCommandFromModule(string cmdName) {
    if (!m_nodePlugin.get()) {
        return nullptr;
    }
    Call<proto::CliCommandId, proto::CliCommand> call(m_nodePlugin.get());
    call.getInput()->set_commandkey(cmdName);

    m_nodePlugin->getCliInterface()->getCliCommandDescription(
            &call, call.getInput().get(), call.getOutput().get(), &call);
    call.wait();

    if (call.Failed()) {
        throw InvalidParameterException(
                "Cannot get command description, error: " + call.ErrorText());
    }

    const proto::CliCommand &cliCmd = *call.getOutput();
    if (cliUtils::isCommandValid(cliCmd)) {
        return make_shared<CommandProtobuf>(cliCmd);
    } else {
        return nullptr;
    }
}

void Executor::execute(CLIList &cliList) {
    shared_ptr<ICommand> command = validateCommand(cliList);

    if (command == nullptr || command == m_moduleCmdSet.getHelpCmd()) {
        // No command for module specified or specified command is help
        // download module's command set and show help
        loadModuleCommandSet();
        stringstream ss;
        CLIUtils::printUsage(ss, &m_module, false);
        CLIUtils::printCmdSetHelp(ss, m_moduleCmdSet);
        log::cout << ss.str();
        return;

    } else if (command == m_localCmdSet.getHelpCmd()) {
        // "First level" help (general for application)
        stringstream ss;
        printMainHelp(ss);
        log::cout << ss.str();
    } else {
        // Fill command's parameters
        if (command->parseParamValues(cliList)) {
            setupOutputsForCommandsLogs();

            if (command->isLocal()) {
                // Execute command locally
                command->execute();

            } else {
                // Execute remotely
                shared_ptr<CommandProtobuf> protoCmd =
                        std::dynamic_pointer_cast<CommandProtobuf>(command);
                if (protoCmd) {
                    executeRemote(protoCmd);

                } else {
                    // Dynamic cast failed
                    throw InvalidParameterException("Unknown command type.");
                }
            }
        } else {
            // Parse parameters failed, show third level (command's) help
            stringstream ss;
            CLIUtils::printCmdHelp(ss, command);
            log::cout << ss.str();
            return;
        }
    }
}

bool Executor::isModuleExistent(std::string moduleName) const {
    for (const auto module : m_modules) {
        if (module.second.getLongKey() == moduleName ||
            module.second.getShortKey() == moduleName) {
            return true;
        }
    }
    return false;
}

void Executor::setModule(std::string moduleName) {
    for (const auto module : m_modules) {
        if (module.second.getLongKey() == moduleName ||
            module.second.getShortKey() == moduleName) {
            // Remember which module was set
            m_module = module.second;

            if (m_module.isLocal()) {
                // Set appropriate command set for local module
                m_moduleCmdSet = m_localModules[moduleName];

            } else {
                // Initialize node plugin if module is remote
                m_nodePlugin.reset(
                        new GenericPluginShadow(m_module.getLongKey()));
                if (!m_nodePlugin->init()) {
                    throw Exception("Plugin unavailable.");
                }

                return;
            }
        }
    }
}

void Executor::addInterface(InterfaceShRef interface, CommandSet &commandSet) {
    // Add every method of interface as command to given command set
    int methodCount = interface->GetDescriptor()->method_count();
    for (int i = 0; i < methodCount; i++) {
        addMethod(interface->GetDescriptor()->method(i), interface, commandSet);
    }
}

void Executor::addMethod(const ::google::protobuf::MethodDescriptor *method,
                         InterfaceShRef interface,
                         CommandSet &commandSet) {
    // Create local command and add it to command set
    std::shared_ptr<CommandProtobufLocal> cmd =
            make_shared<CommandProtobufLocal>(method, interface);
    commandSet.addCmd(cmd);
}

void Executor::addLocalModule(InterfaceShRef interface,
                              const std::string &longKey,
                              const std::string &desc,
                              const std::string &shortKey) {
    if (m_modules.end() != m_modules.find(longKey)) {
        // Specified module already exist
        throw Exception("Trying to add already existing module: " + longKey);
    }

    Module module;
    module.setDesc(desc);
    module.setLongKey(longKey);
    module.setShortKey(shortKey);
    module.setLocal(true);

    // Register module
    m_modules[longKey] = module;

    // Create command set for interface
    addInterface(interface, m_localModules[longKey]);
}

void Executor::addLocalModule(InterfaceShRef interface) {
    addInterface(interface, m_localCmdSet);
}

void Executor::executeRemote(std::shared_ptr<CommandProtobuf> cmd) {
    if (!m_nodePlugin) {
        throw Exception("Wrong initialization of plugin.");
    }
    // Factory to create protobuf messages in runtime
    google::protobuf::DynamicMessageFactory factory;

    // Create prototype of input message based on command input descriptor
    // Factory keeps ownership of memory
    const google::protobuf::Message *prototype_msg =
            factory.GetPrototype(cmd->getInputDesc());

    // Create mutable input message from prototype
    // Ownership passed to the caller, thus use smart pointer to handle it
    MessageShRef in_msg = MessageShRef(prototype_msg->New());

    // Parse parameters - set values stored in command to the protobuf message
    cmd->parseToProtobuf(in_msg.get(), cmd->getInputDesc());

    // Create prototype of output message based on command output descriptor
    // Factory keeps ownership of memory
    const google::protobuf::Message *prototype_out =
            factory.GetPrototype(cmd->getOutputDesc());

    // Create mutable output message from prototype
    // Ownership passed to the caller, thus use smart pointer to handle it
    MessageShRef out_msg = MessageShRef(prototype_out->New());

    CallGeneric call(in_msg, out_msg, m_nodePlugin.get());
    // Remote method call
    m_nodePlugin->getRpcChannel()->genericCallMethod(cmd->getInterfaceId(),
                                                     cmd->getMethodId(), call);

    // Wait for result and print output
    cmd->handleCall(call, out_msg);
}

void Executor::setProgress(double progress, std::ostream &out) {
    uint64_t next = progress * 100;
    uint64_t prev = m_progress * 100;

    if (next != prev) {
        m_progress = progress;
        CLIUtils::printProgressBar(m_progress, out);
    }
}

void Executor::setupOutputsForCommandsLogs() const {
    auto const &prefix = CLIProperties::getCliProperties().getName();

    if (nullptr != getenv("VERBOSE")) {
        log::verbose << log::enable << log::json << log::prefix << prefix;
        log::debug << log::enable << log::json << log::prefix << prefix;
    } else {
        log::verbose << log::disable;
        log::debug << log::disable;
    }

    log::cerr << log::enable << log::json << log::prefix << prefix;
    log::critical << log::enable << log::json << log::prefix << prefix;
    log::cout << log::enable << log::json << log::prefix << prefix;
}

}  // namespace octf
