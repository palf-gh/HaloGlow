# preprocess_pipl_win.cmake
#
# Preprocess a PiPL .r file for Windows with the MSVC preprocessor (/EP: expand
# to stdout with no #line markers, which PiPLtool cannot parse). Referenced only
# by the HaloGlow CMakeLists. Run via `cmake -P`.
#
# Required -D variables:
#   HG_PIPL_COMPILER    full path to cl.exe (CMAKE_CXX_COMPILER)
#   HG_PIPL_OUTPUT      path to write the preprocessed .i
#   HG_PIPL_SOURCE      path to the .r source
#   HG_PIPL_AESDK       After Effects SDK root (Examples/)
#   HG_PIPL_SOURCE_DIR  project source dir (repo root)
#   HG_PIPL_BINARY_DIR  build dir
# Optional:
#   HG_PIPL_DEBUG       if true, defines _DEBUG / HG_DEBUG_BUILD
#   HG_PIPL_HAS_CUDA    if true, defines HG_HAS_CUDA for out-flag parity
#   HG_PIPL_HAS_OPENCL  if true, defines HG_HAS_OPENCL for out-flag parity

if(NOT DEFINED HG_PIPL_COMPILER OR NOT DEFINED HG_PIPL_OUTPUT OR NOT DEFINED HG_PIPL_SOURCE)
	message(FATAL_ERROR "preprocess_pipl_win.cmake requires HG_PIPL_COMPILER, HG_PIPL_OUTPUT, HG_PIPL_SOURCE")
endif()

set(_args
	/nologo
	/EP
	"/I${HG_PIPL_AESDK}/Headers"
	"/I${HG_PIPL_AESDK}/Resources"
	"/I${HG_PIPL_BINARY_DIR}"
	"/I${HG_PIPL_SOURCE_DIR}"
	"/I${HG_PIPL_SOURCE_DIR}/Source"
	/D "MSWindows"
	/D "AE_OS_WIN"
	/D "AE_PROC_INTELx64"
)
if(HG_PIPL_DEBUG)
	list(APPEND _args /D "_DEBUG" /D "HG_DEBUG_BUILD=1")
endif()
if(HG_PIPL_HAS_CUDA)
	list(APPEND _args /D "HG_HAS_CUDA=1")
endif()
if(HG_PIPL_HAS_OPENCL)
	list(APPEND _args /D "HG_HAS_OPENCL=1")
endif()
list(APPEND _args "${HG_PIPL_SOURCE}")

execute_process(
	COMMAND "${HG_PIPL_COMPILER}" ${_args}
	OUTPUT_FILE "${HG_PIPL_OUTPUT}"
	ERROR_VARIABLE _stderr
	RESULT_VARIABLE _result
)

if(NOT _result EQUAL 0)
	message(FATAL_ERROR "PiPL preprocess failed (${_result}): ${_stderr}")
endif()
