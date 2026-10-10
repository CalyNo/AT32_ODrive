#!/usr/bin/env python3
"""Generate the Keil MDK5 project for AT32_ODrive.

The project is derived from the official Artery AT32F435_437 template so the
MDK project schema and target options stay compatible with the vendor pack.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEMPLATE = ROOT / "tools" / "keil_template.uvprojx"
OUTPUT = ROOT / "firmware" / "AT32_ODrive.uvprojx"

USER_SOURCES = [
    "main.c",
    "app.c",
    "scheduler.c",
    "system_time.c",
    "util.c",
    "crc.c",
    "board.c",
    "pwm.c",
    "adc.c",
    "encoder.c",
    "encoder_pll.c",
    "foc.c",
    "pid.c",
    "traptraj.c",
    "calibration.c",
    "controller.c",
    "axis.c",
    "param.c",
    "can_comm.c",
    "uart_comm.c",
    "nvm_config.c",
    "led_pattern.c",
    "status_led.c",
    "usb_cdc.c",
    "ws2812.c",
]

DRIVER_SOURCES = [
    "at32f435_437_crm.c",
    "at32f435_437_gpio.c",
    "at32f435_437_misc.c",
    "at32f435_437_pwc.c",
    "at32f435_437_flash.c",
    "at32f435_437_tmr.c",
    "at32f435_437_adc.c",
    "at32f435_437_spi.c",
    "at32f435_437_usart.c",
    "at32f435_437_can.c",
    "at32f435_437_dma.c",
    "at32f435_437_exint.c",
    "at32f435_437_debug.c",
    "at32f435_437_wdt.c",
    "at32f435_437_usb.c",
]

# USB FS device stack (CDC virtual COM port), mirroring firmware/Makefile.
USB_SOURCES = [
    "usb_core.c",
    "usbd_core.c",
    "usbd_int.c",
    "usbd_sdr.c",
    "cdc_class.c",
    "cdc_desc.c",
]


def file_entry(path: str, file_type: int) -> str:
    name = path.replace("\\", "/").rsplit("/", 1)[-1]
    return f"""            <File>
              <FileName>{name}</FileName>
              <FileType>{file_type}</FileType>
              <FilePath>{path}</FilePath>
            </File>
"""


def group(name: str, files: list[str]) -> str:
    entries = "".join(files)
    return f"""        <Group>
          <GroupName>{name}</GroupName>
          <Files>
{entries}          </Files>
        </Group>
"""


def main() -> None:
    text = TEMPLATE.read_text(encoding="utf-8")

    replacements = {
        r"<TargetName>template</TargetName>": "<TargetName>AT32_ODrive</TargetName>",
        r"<Device>-AT32F435ZMT7</Device>": "<Device>-AT32F435CGT7</Device>",
        r"<PackID>ArteryTek\.AT32F435_437_DFP\.2\.2\.5</PackID>":
            "<PackID>ArteryTek.AT32F435_437_DFP.2.2.8</PackID>",
        r"<Cpu>[^<]*</Cpu>":
            '<Cpu>IRAM(0x20000000,0x60000) IROM(0x08000000,0x100000) '
            'CPUTYPE("Cortex-M4") FPU2 CLOCK(8000000) ELITTLE</Cpu>',
        r"<OutputDirectory>\.\\objects\\</OutputDirectory>":
            "<OutputDirectory>.\\build_keil\\objects\\</OutputDirectory>",
        r"<OutputName>template</OutputName>":
            "<OutputName>AT32_ODrive</OutputName>",
        r"<ListingPath>\.\\listings\\</ListingPath>":
            "<ListingPath>.\\build_keil\\listings\\</ListingPath>",
        r"<uC99>0</uC99>": "<uC99>1</uC99>",
        r"<MiscControls></MiscControls>":
            "<MiscControls>--diag_suppress=188</MiscControls>",
        r"<Define>AT32F435ZMT7,USE_STDPERIPH_DRIVER,AT_START_F435_V1</Define>":
            "<Define>AT32F435CGT7,HEXT_VALUE=8000000</Define>",
        r"<IncludePath>[^<]*</IncludePath>":
            "<IncludePath>.\\Inc;.\\Config;.\\Drivers\\CMSIS\\Core\\Include;"
            ".\\Drivers\\CMSIS\\Device\\AT32F435_437;"
            ".\\Drivers\\AT32F435_437_Firmware_Library\\inc;"
            ".\\Drivers\\USB_Device\\inc</IncludePath>",
        r"<RegisterFilePath>AT32F435ZMT7\$Device\\Include\\at32f435_437\.h\\</RegisterFilePath>":
            "<RegisterFilePath>AT32F435CGT7$Device\\Include\\at32f435_437.h\\</RegisterFilePath>",
        r"<DBRegisterFilePath>AT32F435ZMT7\$Device\\Include\\at32f435_437\.h\\</DBRegisterFilePath>":
            "<DBRegisterFilePath>AT32F435CGT7$Device\\Include\\at32f435_437.h\\</DBRegisterFilePath>",
        r"<SFDFile>\$\$Device:-AT32F435ZMT7\$SVD\\AT32F435xx_v2\.svd</SFDFile>":
            "<SFDFile>$$Device:-AT32F435CGT7$SVD\\AT32F435xx_v2.svd</SFDFile>",
    }

    for pattern, replacement in replacements.items():
        new_text, count = re.subn(pattern, lambda _m, r=replacement: r, text)
        if count == 0:
            raise SystemExit(f"Keil template replacement not found: {pattern}")
        text = new_text

    groups = []
    groups.append(group(
        "User",
        [file_entry(f".\\Src\\{name}", 1) for name in USER_SOURCES],
    ))
    groups.append(group(
        "Startup",
        [file_entry(".\\Startup\\startup_at32f435_437_mdk.s", 2)],
    ))
    groups.append(group(
        "CMSIS",
        [
            file_entry(".\\Drivers\\CMSIS\\Device\\AT32F435_437\\system_at32f435_437.c", 1),
        ],
    ))
    groups.append(group(
        "AT32 Drivers",
        [
            file_entry(f".\\Drivers\\AT32F435_437_Firmware_Library\\src\\{name}", 1)
            for name in DRIVER_SOURCES
        ],
    ))
    groups.append(group(
        "USB Device",
        [
            file_entry(f".\\Drivers\\USB_Device\\src\\{name}", 1)
            for name in USB_SOURCES
        ],
    ))
    groups.append(group(
        "Config",
        [
            file_entry(".\\Config\\at32f435_437_conf.h", 5),
            file_entry(".\\Config\\usb_conf.h", 5),
            file_entry(".\\Inc\\config.h", 5),
            file_entry(".\\Inc\\board.h", 5),
            file_entry(".\\Inc\\nvm_config.h", 5),
        ],
    ))

    groups_xml = "      <Groups>\n" + "".join(groups) + "      </Groups>"
    text, count = re.subn(r"      <Groups>.*?</Groups>",
                          lambda _m, r=groups_xml: r,
                          text,
                          flags=re.S)
    if count != 1:
        raise SystemExit("Keil template <Groups> block not found exactly once")

    OUTPUT.write_text(text, encoding="utf-8")
    print(f"Wrote {OUTPUT}")


if __name__ == "__main__":
    main()
