target_sources(octf
PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/ITraceParser.h
	${CMAKE_CURRENT_LIST_DIR}/TraceFileParser.cpp
	${CMAKE_CURRENT_LIST_DIR}/TraceFileReader.h
	${CMAKE_CURRENT_LIST_DIR}/TraceFileParser.h
	${CMAKE_CURRENT_LIST_DIR}/TraceFileReader.cpp
	${CMAKE_CURRENT_LIST_DIR}/TraceEventHandler.h
	${CMAKE_CURRENT_LIST_DIR}/IoTraceEventHandlerJsonPrinter.h
	${CMAKE_CURRENT_LIST_DIR}/TraceEventHandlerJsonPrinter.h
)
