# Zapojení displeje

4,0" TFT displej 480 × 320 s rozhraním SPI a řadičem **ST7796S** připojený k desce
**LaskaKit ESPlan v1.1**. Firmware na něm zobrazuje azimut, elevaci a stav desek AZ/EL,
stejně jako webová stránka.

Displej odpovídá modulu z lcdwiki „4.0inch SPI Module ST7796“ ve dvou provedeních:

- **MSP4030** – bez dotyku (kus, který je teď k dispozici),
- **MSP4031** – s kapacitním dotykem, řadič **FT6336U** na sběrnici I²C (adresa 0x38).

Firmware dotyk zatím neobsluhuje; jeho piny níže jsou připravené pro provedení MSP4031.

## Lišta JP1 na ESPlan v1.1

Podle schématu `HW/old/ESPlan_v1_1.pdf` v repozitáři
[LaskaKit/ESPlan](https://github.com/LaskaKit/ESPlan):

| Pin JP1 | Signál | Pin JP1 | Signál |
|---|---|---|---|
| 1 | 3V3 | 9 | TX1 = IO4 (**RS485, nepoužívat**) |
| 2 | VCC (jen napájecí vstup) | 10 | IO16 |
| 3 | GND | 11 | IO13 (HSPI MOSI) |
| 4 | IO34 (jen vstup) | 12 | IO12 (HSPI MISO, strapping) |
| 5 | IO35 (jen vstup) | 13 | IO14 (HSPI SCK) |
| 6 | IO32 (SCL) | 14 | IO39 / SENSOR_VN (jen vstup) |
| 7 | IO33 (SDA) | 15 | RX1 = IO36 (**RS485, nepoužívat**) |
| 8 | IO0 (strapping) | 16 | EN (reset ESP32) |

## Zapojení

| Pin displeje | ESPlan v1.1 | Pin JP1 | Poznámka |
|---|---|---|---|
| 1 VCC | 3V3 | 1 | modul pracuje i na 3,3 V |
| 2 GND | GND | 3 | |
| 3 LCD_CS | IO0 | 8 | viz [IO0 jako CS](#io0-jako-cs) |
| 4 LCD_RST | EN | 16 | displej se resetuje spolu s ESP32 |
| 5 LCD_RS (DC) | IO16 | 10 | |
| 6 SDI (MOSI) | IO13 | 11 | |
| 7 SCK | IO14 | 13 | |
| 8 LED | 3V3 | 1 | podsvícení trvale zapnuté, asi 100 mA |
| 9 SDO (MISO) | **nezapojovat** | – | z displeje se nečte; IO12 rozhoduje o startu ESP32 |
| 10 CTP_SCL | IO32 | 6 | jen provedení s dotykem; jde i na konektor μŠup I²C |
| 11 CTP_RST | EN | 16 | jen provedení s dotykem |
| 12 CTP_SDA | IO33 | 7 | jen provedení s dotykem; jde i na konektor μŠup I²C |
| 13 CTP_INT | IO39 | 14 | jen provedení s dotykem |
| 14 SD_CS | **nezapojovat** | – | slot na kartu SD na displeji se nepoužívá |

Piny jsou ve firmwaru v `include/display.h`.

## Na co dát pozor

- **IO4 (TX1) a IO36 (RX1)** na liště patří sběrnici RS485 k deskám AZ/EL.
- **Pin VCC (2) je jen napájecí vstup**: do desky vede přes diodu, takže při napájení
  z USB nebo PoE na něm nic není. Displej napájej z 3V3. Z VCC jen tehdy, když přes něj
  napájíš celou desku 5 V – podsvícení pak bude jasnější.
- **Do slotu na kartu SD na ESPlanu nic nedávej**: karta sdílí s displejem IO12–IO14.
- **IO12 nesmí být při startu nahoře** (v1.1 na něm nemá pull-down), proto SDO displeje
  zůstává nezapojené.
- **Vodiče SPI drž krátké**, ideálně do 15 cm. Při delších vodičích sniž
  `DISPLAY_SPI_CLOCK_HZ` (teď 26 MHz).
- **Pull-up odpory na I²C** (pro dotyk) se na v1.1 připojují pájecí propojkou
  `I2C_PULL_UP`. Dotyková vrstva má obvykle vlastní, takže to nejspíš půjde i bez ní.

### IO0 jako CS

Na liště v1.1 zbývá jediný volný výstup (IO16), a ten potřebuje DC. CS proto jde na IO0.
IO0 je strapping pin – jeho úroveň při startu určuje, jestli ESP32 naběhne normálně, nebo
do nahrávání firmwaru. Pro normální start musí být nahoře, a v té úrovni je CS displeje
neaktivní, takže se to nepere. Při nahrávání firmwaru ho převodník USB stáhne dolů a displej
je chvíli vybraný, což nevadí. Kdyby to přesto zlobilo, jde CS displeje připojit natvrdo na
GND – displej je na sběrnici SPI sám.

## Doladění obrazu

Orientace a pořadí barev se liší kus od kusu. Přepínače jsou v `include/display.h`:

| Projev | Úprava |
|---|---|
| obraz vzhůru nohama | přehodit `DISPLAY_MIRROR_X` i `DISPLAY_MIRROR_Y` |
| bílé pozadí místo tmavého | `DISPLAY_INVERT_COLORS true` |
| prohozená červená a modrá (tlačítko STOP má být červené) | `DISPLAY_BGR false` |
| rozsypané body, šum | snížit `DISPLAY_SPI_CLOCK_HZ` |

## Stav ověření

Zapojení vychází ze schématu ESPlan v1.1 a z dokumentace modulu na lcdwiki; na hardwaru
zatím ověřené není. Firmware s displejem v jednotce naběhne a obsluhu displeje spustí
(log `DISPLAY: Display started`), obraz na displeji zatím nikdo nepotvrdil.
