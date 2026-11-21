# Third-Party Notices & Attributions

Polyend Tracker Community Edition (CE) incorporates or links against the following third-party open-source and binary components:

---

## 1. ARM CMSIS & CMSIS-DSP
* **Location**: `platforms/imx/CMSIS/`, `platforms/mk66/3rdparty/teensy_core/`
* **License**: Apache License 2.0 / BSD-3-Clause
* **Copyright**: Copyright (c) ARM Limited. All rights reserved.

## 2. NXP MCUXpresso SDK (Kinetis K66 & i.MX RT1050/RT1060)
* **Location**: `platforms/imx/drivers/`, `platforms/imx/device/`, `platforms/imx/usb/`, `platforms/imx/sdmmc/`, `platforms/mk66/3rdparty/SD/`, `platforms/mk66/3rdparty/I2C/`, `platforms/mk66/3rdparty/SDRAM/`
* **License**: BSD-3-Clause
* **Copyright**: Copyright (c) Freescale Semiconductor, Inc. / Copyright 2016-2022 NXP.

## 3. FatFs - Generic FAT Filesystem Module
* **Location**: `platforms/imx/fatfs/`, `platforms/mk66/3rdparty/SD/fatfs/`
* **License**: FatFs License (BSD-style)
* **Copyright**: Copyright (C) ChaN, 2019. All rights reserved.

## 4. Teensy Cores & Teensy Audio Library (PJRC)
* **Location**: `platforms/mk66/3rdparty/teensy_core/`, `platforms/mk66/3rdparty/Audio/`, `core/libs/Audio/`
* **License**: MIT / PJRC Permissive License
* **Copyright**: Copyright (c) 2014-2018 Paul Stoffregen, PJRC.COM, LLC.

## 5. Liberation Sans Fonts (UI Bitmap Font Source)
* **Location**: `core/libs/fonts/LiberationSans-*_L4.{h,cpp}` (4-bit `L4` antialiased glyph bitmaps)
* **License**: SIL Open Font License 1.1 (OFL-1.1)
* **Copyright**: Digitized data copyright (c) 2010 Google Corporation; Copyright (c) 2012 Red Hat, Inc.

## 6. Embedded Template Library (ETL)
* **Location**: `util/3rdparty/etl/`
* **License**: MIT License
* **Copyright**: Copyright (c) 2016 jwellbelove (John Wellbelove).

## 7. magic_enum
* **Location**: `util/3rdparty/magic_enum/`
* **License**: MIT License
* **Copyright**: Copyright (c) 2019 - 2022 Daniil Goncharov.

## 8. FastCRC
* **Location**: `platforms/mk66/3rdparty/FastCRC/`, `platforms/imx/3rdparty/FastCRC/`
* **License**: MIT / GPLv3 dual-licensed (used under MIT/BSD terms where applicable)
* **Copyright**: Copyright (c) 2014-2016 Frank Boesing.

## 9. Polyend Precompiled Factory Synthesizer Plugins (`*.psyn`)
* **Location**: `synth/prebuilt/`
* **License**: Polyend Precompiled Factory Synthesizer Plugins License (`synth/prebuilt/LICENSE-PROPRIETARY.txt`)
* **Copyright**: Copyright (c) 2023-2026 Polyend Sp. z o.o. All rights reserved.

## 10. Edisyn (synth parameter data for the EUCLID synth editor)
* **Location**: `euclid-synths/` (synth definition files and the scripts that generate them)
* **License**: Apache License 2.0 (`euclid-synths/LICENSE-Edisyn.txt`)
* **Copyright**: Copyright 2017 by Sean Luke. Parameter names, ranges, numbering and choice lists are derived from Edisyn's editors (https://github.com/eclab/edisyn).

## 11. jv1080-sysex-manager (JV-1080 parameter tables for the EUCLID synth editor)

* **License**: MIT License (`euclid-synths/LICENSE-jv1080-sysex-manager.txt`)
* **Copyright**: Copyright (c) 2025 Lt-Blmr. `euclid-synths/JV1080.syn` is generated from `RolandJV1080.ini` in https://github.com/sonic-codex/jv1080-sysex-manager, a transcription of Roland's JV-1080 MIDI implementation tables.
