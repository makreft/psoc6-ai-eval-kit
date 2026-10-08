cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(clap_rules)

# Mikrofon ueber die Infineon-HAL (PDM/PCM). Zephyr baut diese HAL-Teile fuer CAT1A nicht selbst,
# deshalb werden sie hier als Anwendungsquellen hinzugefuegt (Pfade siehe Protokoll Abschnitt 60).
set(HAL_DIR ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/mtb-hal-cat1)
set(PDL_DIR ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/mtb-pdl-cat1)
target_sources(app PRIVATE
  src/main.c
  src/mic.c
  ${HAL_DIR}/source/cyhal_pdmpcm.c
  ${HAL_DIR}/source/cyhal_audio_common.c
  ${PDL_DIR}/drivers/source/cy_pdm_pcm.c
)
