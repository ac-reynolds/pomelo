find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)
execute_process(COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_VARIABLE nanobind_dir OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
find_package(nanobind CONFIG REQUIRED PATHS "${nanobind_dir}")
nanobind_add_module(_native bindings/bindings.cpp)
target_link_libraries(_native PRIVATE application)
install(TARGETS _native LIBRARY DESTINATION pomelo COMPONENT python)
