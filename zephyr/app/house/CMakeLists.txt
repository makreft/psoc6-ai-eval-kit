cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(house LANGUAGES C CXX)

# TensorFlow Lite Micro: west-Modul aus zephyr/manifest/west.yml (Chirale_TensorFlowLite v2.0.0)
set(TFLM_DIR "${ZEPHYR_BASE}/../modules/lib/chirale-tflm/src" CACHE PATH "TFLM-Quellbaum")
file(GLOB_RECURSE TFLM_SRC
  ${TFLM_DIR}/tensorflow/*.cpp ${TFLM_DIR}/tensorflow/*.cc ${TFLM_DIR}/tensorflow/*.c
  ${TFLM_DIR}/third_party/cmsis_nn/Source/*.c)
list(FILTER TFLM_SRC EXCLUDE REGEX "system_setup\\.cpp$|/peripherals/|_test\\.(cpp|cc)$|/testing/|/benchmarks/|/examples/")
target_sources(app PRIVATE ${TFLM_SRC})
set_source_files_properties(${TFLM_SRC} PROPERTIES COMPILE_OPTIONS
  "-Wno-unused-parameter;-Wno-sign-compare;-Wno-missing-field-initializers;-Wno-unused-variable;-Wno-deprecated-declarations;-Wno-unused-function")
# Modell und Merkmalskonfiguration direkt aus der Trainings-Pipeline (eine Quelle, keine Kopien)
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../ml/clap/out)
zephyr_include_directories(
  ${TFLM_DIR} ${TFLM_DIR}/third_party/flatbuffers/include ${TFLM_DIR}/third_party/gemmlowp ${TFLM_DIR}/third_party/ruy
  ${TFLM_DIR}/third_party/kissfft ${TFLM_DIR}/third_party/cmsis_nn ${TFLM_DIR}/third_party/cmsis_nn/Include ${TFLM_DIR}/third_party/cmsis/CMSIS/Core/Include)
zephyr_compile_definitions(TF_LITE_STATIC_MEMORY TF_LITE_STRIP_ERROR_STRINGS TF_LITE_MCU_DEBUG_LOG CMSIS_NN ARDUINO=100 ARDUINO_EXCLUDE_CODE)

set(HAL_DIR ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/mtb-hal-cat1)
set(PDL_DIR ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/mtb-pdl-cat1)
target_sources(app PRIVATE
  src/main.c src/bt_presence.c src/clap.cpp src/mic.c src/cyhal_dma_stub.c
  ${HAL_DIR}/source/cyhal_pdmpcm.c ${HAL_DIR}/source/cyhal_audio_common.c ${PDL_DIR}/drivers/source/cy_pdm_pcm.c)
