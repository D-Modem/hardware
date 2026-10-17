#!/usr/bin/env python3
"""pico-serial netlist: every part and connection on the board, in one place.

Writes, next to this script:
  pico-serial.net   KiCad netlist (import into Pcbnew: File > Import > Netlist)
  NETLIST.md        the same nets, readable: by net and by part
  bom-jlc.csv       JLC assembly BOM (Comment, Designator, Footprint, LCSC)

Checks before writing (exit 1 on any error):
  - every pin of every part is on a net or listed as not connected
  - no net has fewer than two pins
  - no net has two outputs driving it
  - every RS-232 line has exactly one driver and one receiver
  - every power input sits on a supply net
  - with --kicad-sym/--kicad-fp: pin numbers and names match KiCad's
    symbol, and every pin has a pad in the footprint

Spec: ../../SPEC.md (section numbers below refer to it).
"""
import argparse, datetime, os, re, sys, uuid
from collections import defaultdict

# ---------------------------------------------------------------------------
# Pin tables. RP2040, flash, LDO, USBLC6 and connectors are copied from
# KiCad's symbol library (checked with --kicad-sym). KiCad has no symbol for
# the two RS-232 chips; theirs come from the datasheets:
#   SP3238E  MaxLinear rev 1.0.1, pin description table
#   MAX3243E TI SLLS657E, Table 6-2 (DB/DW/PW 28-pin)
# Pin types follow KiCad's names.
# ---------------------------------------------------------------------------
P, PI, PO, I, O, B, NC = "passive", "power_in", "power_out", "input", "output", "bidirectional", "no_connect"

RP2040 = {
    "1": ("IOVDD", PI), "2": ("GPIO0", B), "3": ("GPIO1", B), "4": ("GPIO2", B), "5": ("GPIO3", B),
    "6": ("GPIO4", B), "7": ("GPIO5", B), "8": ("GPIO6", B), "9": ("GPIO7", B), "10": ("IOVDD", P),
    "11": ("GPIO8", B), "12": ("GPIO9", B), "13": ("GPIO10", B), "14": ("GPIO11", B), "15": ("GPIO12", B),
    "16": ("GPIO13", B), "17": ("GPIO14", B), "18": ("GPIO15", B), "19": ("TESTEN", I), "20": ("XIN", I),
    "21": ("XOUT", O), "22": ("IOVDD", P), "23": ("DVDD", PI), "24": ("SWCLK", I), "25": ("SWDIO", B),
    "26": ("RUN", I), "27": ("GPIO16", B), "28": ("GPIO17", B), "29": ("GPIO18", B), "30": ("GPIO19", B),
    "31": ("GPIO20", B), "32": ("GPIO21", B), "33": ("IOVDD", P), "34": ("GPIO22", B), "35": ("GPIO23", B),
    "36": ("GPIO24", B), "37": ("GPIO25", B), "38": ("GPIO26/ADC0", B), "39": ("GPIO27/ADC1", B),
    "40": ("GPIO28/ADC2", B), "41": ("GPIO29/ADC3", B), "42": ("IOVDD", P), "43": ("ADC_AVDD", PI),
    "44": ("VREG_VIN", PI), "45": ("VREG_VOUT", PO), "46": ("USB_DM", B), "47": ("USB_DP", B),
    "48": ("USB_VDD", PI), "49": ("IOVDD", P), "50": ("DVDD", P), "51": ("QSPI_SD3", B),
    "52": ("QSPI_SCLK", O), "53": ("QSPI_SD0", B), "54": ("QSPI_SD2", B), "55": ("QSPI_SD1", B),
    "56": ("~{QSPI_SS}", O), "57": ("GND", PI),
}
SP3238E = {
    "1": ("C2+", P), "2": ("GND", PI), "3": ("C2-", P), "4": ("V-", P), "5": ("T1OUT", O),
    "6": ("T2OUT", O), "7": ("T3OUT", O), "8": ("R1IN", I), "9": ("R2IN", I), "10": ("T4OUT", O),
    "11": ("R3IN", I), "12": ("T5OUT", O), "13": ("ONLINE", I), "14": ("~{SHUTDOWN}", I),
    "15": ("STATUS", O), "16": ("R1OUTB", O),  # non-inverting R1 output, active in shutdown
    "17": ("T5IN", I), "18": ("R3OUT", O), "19": ("T4IN", I), "20": ("R2OUT", O), "21": ("R1OUT", O),
    "22": ("T3IN", I), "23": ("T2IN", I), "24": ("T1IN", I), "25": ("C1-", P), "26": ("VCC", PI),
    "27": ("V+", P), "28": ("C1+", P),
}
MAX3243E = {
    "1": ("C2+", P), "2": ("C2-", P), "3": ("V-", P), "4": ("RIN1", I), "5": ("RIN2", I),
    "6": ("RIN3", I), "7": ("RIN4", I), "8": ("RIN5", I), "9": ("DOUT1", O), "10": ("DOUT2", O),
    "11": ("DOUT3", O), "12": ("DIN3", I), "13": ("DIN2", I), "14": ("DIN1", I), "15": ("ROUT5", O),
    "16": ("ROUT4", O), "17": ("ROUT3", O), "18": ("ROUT2", O), "19": ("ROUT1", O),
    "20": ("ROUT2B", O),  # always-active non-inverting RIN2 output
    "21": ("INVALID", O), "22": ("~{FORCEOFF}", I), "23": ("FORCEON", I), "24": ("C1-", P),
    "25": ("GND", PI), "26": ("VCC", PI), "27": ("V+", P), "28": ("C1+", P),
}
W25Q = {"1": ("~{CS}", I), "2": ("DO/IO_{1}", B), "3": ("~{WP}/IO_{2}", B), "4": ("GND", PI),
        "5": ("DI/IO_{0}", B), "6": ("CLK", I), "7": ("~{HOLD}/~{RESET}/IO_{3}", B), "8": ("VCC", PI)}
AP2112K = {"1": ("VIN", PI), "2": ("GND", PI), "3": ("EN", I), "4": ("NC", NC), "5": ("VOUT", PO)}
USBLC6 = {"1": ("I/O1", P), "2": ("GND", P), "3": ("I/O2", P), "4": ("I/O2", P), "5": ("VBUS", P), "6": ("I/O1", P)}
USB_C16 = {"A1": ("GND", P), "A4": ("VBUS", P), "A5": ("CC1", B), "A6": ("D+", B), "A7": ("D-", B),
           "A8": ("SBU1", B), "A9": ("VBUS", P), "A12": ("GND", P), "B1": ("GND", P), "B4": ("VBUS", P),
           "B5": ("CC2", B), "B6": ("D+", B), "B7": ("D-", B), "B8": ("SBU2", B), "B9": ("VBUS", P),
           "B12": ("GND", P), "SH": ("SHIELD", P)}
DB25F = {str(n): (str(n) if n <= 13 else f"P{n}", P) for n in range(1, 26)}
XTAL4 = {"1": ("1", P), "2": ("G", P), "3": ("3", P), "4": ("G", P)}
TWO = {"1": ("~", P), "2": ("~", P)}
DIODE = {"1": ("K", P), "2": ("A", P)}
SW = {"1": ("1", P), "2": ("2", P)}
TP = {"1": ("1", P)}

# KiCad library symbol for each pin table (lib, symbol); None = no KiCad symbol
SYMBOLS = {
    id(RP2040): ("MCU_RaspberryPi", "RP2040"), id(W25Q): ("Memory_Flash", "W25Q128JVS"),
    id(AP2112K): ("Regulator_Linear", "AP2112K-3.3"), id(USBLC6): ("Power_Protection", "USBLC6-2SC6"),
    id(USB_C16): ("Connector", "USB_C_Receptacle_USB2.0_16P"), id(DB25F): ("Connector", "DB25_Socket"),
    id(XTAL4): ("Device", "Crystal_GND24"), id(DIODE): None, id(SW): ("Switch", "SW_Push"),
    id(TP): ("Connector", "TestPoint"), id(SP3238E): None, id(MAX3243E): None,
}

FP_R = "Resistor_SMD:R_0402_1005Metric"
FP_C = "Capacitor_SMD:C_0402_1005Metric"
FP_C6 = "Capacitor_SMD:C_0603_1608Metric"

# value -> (LCSC part, footprint); JLC "basic" parts unless noted
PASSIVES = {
    "100nF": ("C1525", FP_C), "1uF": ("C52923", FP_C), "1uF 50V": ("C15849", FP_C6),
    "220nF": ("C16772", FP_C), "10uF": ("C19702", FP_C6), "15pF": ("C1548", FP_C),
    "4.7nF": ("C1538", FP_C), "27R": ("C25100", FP_R),  # 27R is "extended"
    "5.1k": ("C25905", FP_R), "1k": ("C11702", FP_R), "10k": ("C25744", FP_R),
    "100k": ("C25741", FP_R), "1M": ("C26083", FP_R), "0R": ("C17168", FP_R),
}

# ---------------------------------------------------------------------------
parts = {}      # ref -> dict
nets = defaultdict(list)   # net -> [(ref, pin)]
noconnect = set()          # (ref, pin)
counters = defaultdict(int)


def part(prefix, value, pins, footprint, lcsc="", note="", dnp=False, assembly="jlc", desc="", ref=None):
    if ref is None:
        counters[prefix] += 1
        ref = f"{prefix}{counters[prefix]}"
    assert ref not in parts, ref
    parts[ref] = dict(ref=ref, value=value, pins=pins, footprint=footprint, lcsc=lcsc,
                      note=note, dnp=dnp, assembly=assembly, desc=desc)
    return ref


def conn(net, *nodes):
    """conn("NET", ("U1","2"), ...) puts each (ref, pin) on NET."""
    for ref, pin in nodes:
        assert pin in parts[ref]["pins"], f"{ref} has no pin {pin}"
        for n, members in nets.items():
            if (ref, pin) in members:
                raise SystemExit(f"{ref}.{pin} is already on {n}, can't also join {net}")
        nets[net].append((ref, pin))


def passive(kind, value, a, b, note="", dnp=False):
    """A two-pin R or C between nets a and b."""
    lcsc, fp = PASSIVES[value]
    ref = part(kind, value.split()[0], TWO, fp, lcsc, note, dnp)
    conn(a, (ref, "1"))
    conn(b, (ref, "2"))
    return ref


def nc(ref, *pins):
    for p in pins:
        noconnect.add((ref, p))


def tp(net, label):
    ref = part("TP", label, TP, "TestPoint:TestPoint_Pad_D1.0mm", assembly="none")
    conn(net, (ref, "1"))


# ===========================================================================
# The circuit
# ===========================================================================
GND, VBUS_IN, VBUS, V33, V11 = "GND", "VBUS_IN", "VBUS", "+3V3", "+1V1"
SUPPLIES = {GND, VBUS_IN, VBUS, V33, V11}

# --- §4.4 USB-C (UFP) -------------------------------------------------------
J1 = part("J", "USB_C", USB_C16, "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12", "C165948",
          desc="HRO TYPE-C-31-M-12")
conn(GND, *[(J1, p) for p in ("A1", "A12", "B1", "B12")])
conn(VBUS_IN, *[(J1, p) for p in ("A4", "A9", "B4", "B9")])
conn("CC1", (J1, "A5"))
conn("CC2", (J1, "B5"))
conn("USB_CONN_DP", (J1, "A6"), (J1, "B6"))
conn("USB_CONN_DM", (J1, "A7"), (J1, "B7"))
conn("USB_SHIELD", (J1, "SH"))
nc(J1, "A8", "B8")
passive("R", "5.1k", "CC1", GND, "CC1 Rd: separate resistor per CC pin")
passive("R", "5.1k", "CC2", GND, "CC2 Rd")
passive("R", "1M", "USB_SHIELD", GND, "shell bleed")
passive("C", "4.7nF", "USB_SHIELD", GND, "shell to GND, RF")

# --- §4.3 power ---------------------------------------------------------------
F1 = part("F", "0.5A", TWO, "Fuse:Fuse_1206_3216Metric", "C163512", desc="Littelfuse 1206L050YR PTC")
conn(VBUS_IN, (F1, "1")); conn(VBUS, (F1, "2"))
D1 = part("D", "SMF5.0A", DIODE, "Diode_SMD:D_SOD-123F", "C151296", desc="5 V TVS, unidirectional")
conn(VBUS, (D1, "1")); conn(GND, (D1, "2"))
U_ESD = part("U", "USBLC6-2SC6", USBLC6, "Package_TO_SOT_SMD:SOT-23-6", "C7519", ref="U4")
conn("USB_CONN_DP", (U_ESD, "1"), (U_ESD, "6"))
conn("USB_CONN_DM", (U_ESD, "3"), (U_ESD, "4"))
conn(VBUS, (U_ESD, "5")); conn(GND, (U_ESD, "2"))
U_LDO = part("U", "AP2112K-3.3", AP2112K, "Package_TO_SOT_SMD:SOT-23-5", "C51118", ref="U3")
conn(VBUS, (U_LDO, "1"), (U_LDO, "3")); conn(GND, (U_LDO, "2")); conn(V33, (U_LDO, "5"))
nc(U_LDO, "4")
passive("C", "10uF", VBUS, GND, "LDO input")
passive("C", "10uF", V33, GND, "LDO output, 3V3 bulk")
passive("R", "100k", VBUS, "VBUS_SENSE", "VBUS divider top")
passive("R", "100k", "VBUS_SENSE", GND, "VBUS divider bottom")

# --- §4.2 RP2040 ------------------------------------------------------------
U1 = part("U", "RP2040", RP2040, "Package_DFN_QFN:QFN-56-1EP_7x7mm_P0.4mm_EP3.2x3.2mm", "C2040", ref="U1")
for p in ("1", "10", "22", "33", "42", "49"):
    conn(V33, (U1, p))
    passive("C", "100nF", V33, GND, f"IOVDD pin {p}")
conn(V33, (U1, "48")); passive("C", "100nF", V33, GND, "USB_VDD")
conn(V33, (U1, "43")); passive("C", "100nF", V33, GND, "ADC_AVDD")
conn(V33, (U1, "44")); passive("C", "1uF", V33, GND, "VREG_VIN")
conn(V11, (U1, "45"), (U1, "23"), (U1, "50"))
passive("C", "1uF", V11, GND, "VREG_VOUT")
passive("C", "100nF", V11, GND, "DVDD pin 23")
passive("C", "100nF", V11, GND, "DVDD pin 50")
conn(GND, (U1, "57"), (U1, "19"))          # EP, TESTEN
conn("USB_DP", (U1, "47")); conn("USB_DM", (U1, "46"))
passive("R", "27R", "USB_CONN_DP", "USB_DP", "USB D+ series")
passive("R", "27R", "USB_CONN_DM", "USB_DM", "USB D- series")

# crystal: 15 pF each side, 1k in series with XOUT
Y1 = part("Y", "12MHz", XTAL4, "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm", "C20625731", desc="Abracon ABM8-272-T3")
conn("XIN", (U1, "20"), (Y1, "1"))
conn("XOUT", (U1, "21"))
conn("XTAL_OUT", (Y1, "3"))
conn(GND, (Y1, "2"), (Y1, "4"))
passive("R", "1k", "XOUT", "XTAL_OUT", "crystal drive limit")
passive("C", "15pF", "XIN", GND, "crystal load")
passive("C", "15pF", "XTAL_OUT", GND, "crystal load")

# flash
U2 = part("U", "W25Q128JVS", W25Q, "Package_SO:SOIC-8_5.3x5.3mm_P1.27mm", "C97521", ref="U2")
conn("QSPI_SS", (U1, "56"), (U2, "1"))
conn("QSPI_SD1", (U1, "55"), (U2, "2"))
conn("QSPI_SD2", (U1, "54"), (U2, "3"))
conn("QSPI_SD0", (U1, "53"), (U2, "5"))
conn("QSPI_SCLK", (U1, "52"), (U2, "6"))
conn("QSPI_SD3", (U1, "51"), (U2, "7"))
conn(V33, (U2, "8")); conn(GND, (U2, "4"))
passive("C", "100nF", V33, GND, "flash VCC")

# BOOTSEL: QSPI_SS to GND through 1k; RESET: RUN to GND
SW1 = part("SW", "BOOTSEL", SW, "Button_Switch_SMD:SW_SPST_TS-1088-xR020", "C720477", desc="XUNPU TS-1088-AR02016")
passive("R", "1k", "QSPI_SS", "BOOTSEL_SW", "BOOTSEL")
conn("BOOTSEL_SW", (SW1, "1")); conn(GND, (SW1, "2"))
SW2 = part("SW", "RESET", SW, "Button_Switch_SMD:SW_SPST_TS-1088-xR020", "C720477", desc="XUNPU TS-1088-AR02016")
conn("RUN", (U1, "26"), (SW2, "1")); conn(GND, (SW2, "2"))
passive("R", "10k", V33, "RUN", "RUN pull-up; RP2040 has a weak internal one, this adds noise margin")
conn("SWCLK", (U1, "24")); conn("SWDIO", (U1, "25"))

# --- §4.5 RS-232: chip A (DCE, SP3238E) and chip B (DTE, MAX3243E) -----------
UA = part("U", "SP3238EEA", SP3238E, "Package_SO:SSOP-28_5.3x10.2mm_P0.65mm", "C49669",
          desc="MaxLinear SP3238EEA-L/TR, chip A, DCE", ref="U5")
UB = part("U", "MAX3243EIDBR", MAX3243E, "Package_SO:SSOP-28_5.3x10.2mm_P0.65mm", "C353887",
          desc="TI MAX3243EIDBR, chip B, DTE", ref="U6")


def charge_pump(u, c1p, c1m, c2p, c2m, vp, vm, vcc, gnd, tag):
    conn(f"{tag}_C1P", (u, c1p)); conn(f"{tag}_C1M", (u, c1m))
    conn(f"{tag}_C2P", (u, c2p)); conn(f"{tag}_C2M", (u, c2m))
    conn(f"{tag}_VP", (u, vp)); conn(f"{tag}_VM", (u, vm))
    conn(V33, (u, vcc)); conn(GND, (u, gnd))
    passive("C", "220nF", f"{tag}_C1P", f"{tag}_C1M", f"chip {tag[-1]} C1 flying")
    passive("C", "1uF 50V", f"{tag}_C2P", f"{tag}_C2M", f"chip {tag[-1]} C2 flying")
    passive("C", "1uF 50V", f"{tag}_VP", GND, f"chip {tag[-1]} V+ reservoir")
    passive("C", "1uF 50V", f"{tag}_VM", GND, f"chip {tag[-1]} V- reservoir")
    passive("C", "1uF", V33, GND, f"chip {tag[-1]} VCC")
    passive("C", "100nF", V33, GND, f"chip {tag[-1]} VCC")


charge_pump(UA, "28", "25", "1", "3", "27", "4", "26", "2", "CPA")
charge_pump(UB, "28", "24", "1", "2", "27", "3", "26", "25", "CPB")

J2 = part("J", "DB25_Female", DB25F, "Connector_Dsub:DSUB-25_Socket_EdgeMount_P2.77mm", "C19601171",
          assembly="hand", desc="MH Connectors MHDB25SS, solder cups straddling the board edge")

# Each DB25 signal pin: one driver from one chip, one receiver from the other.
#             net      DB25  chip A pin  chip B pin
RS232 = [("RS_TXD", "2", "8", "9"),     # A R1IN   / B DOUT1
         ("RS_RXD", "3", "5", "4"),     # A T1OUT  / B RIN1
         ("RS_RTS", "4", "9", "10"),    # A R2IN   / B DOUT2
         ("RS_CTS", "5", "6", "5"),     # A T2OUT  / B RIN2
         ("RS_DSR", "6", "7", "6"),     # A T3OUT  / B RIN3
         ("RS_DCD", "8", "10", "7"),    # A T4OUT  / B RIN4
         ("RS_DTR", "20", "11", "11"),  # A R3IN   / B DOUT3
         ("RS_RI", "22", "12", "8")]    # A T5OUT  / B RIN5
for net, db, a, b in RS232:
    conn(net, (J2, db), (UA, a), (UB, b))
conn(GND, (J2, "7"))
conn("DB25_FG", (J2, "1"))
passive("R", "0R", "DB25_FG", GND, "frame ground to GND; leave off to float pin 1")
nc(J2, *[str(n) for n in range(1, 26) if str(n) not in {"1", "2", "3", "4", "5", "6", "7", "8", "20", "22"}])

# --- §4.6 GPIO map ------------------------------------------------------------
GPIO = {  # GPIO: (net, chip pin)
    0: ("A_TX", (UA, "24")),      # UART0 TX set A -> T1IN -> pin 3
    1: ("A_RX", (UA, "21")),      # UART0 RX set A <- R1OUT <- pin 2
    2: ("A_CTS", (UA, "20")),     # UART0 CTS set A <- R2OUT <- pin 4 (computer's RTS)
    3: ("A_RTS", (UA, "23")),     # UART0 RTS set A -> T2IN -> pin 5 (CTS to computer)
    4: ("A_DSR", (UA, "22")),     # T3IN -> pin 6
    5: ("A_DCD", (UA, "19")),     # T4IN -> pin 8
    6: ("A_RI", (UA, "17")),      # T5IN -> pin 22
    7: ("A_DTR", (UA, "18")),     # R3OUT <- pin 20
    8: ("B_DTR", (UB, "12")),     # DIN3 -> pin 20
    9: ("B_DSR", (UB, "17")),     # ROUT3 <- pin 6
    10: ("B_DCD", (UB, "16")),    # ROUT4 <- pin 8
    11: ("B_RI", (UB, "15")),     # ROUT5 <- pin 22
    12: ("B_TX", (UB, "14")),     # UART0 TX set B -> DIN1 -> pin 2
    13: ("B_RX", (UB, "19")),     # UART0 RX set B <- ROUT1 <- pin 3
    14: ("B_CTS", (UB, "18")),    # UART0 CTS set B <- ROUT2 <- pin 5
    15: ("B_RTS", (UB, "13")),    # UART0 RTS set B -> DIN2 -> pin 4
    16: ("A_SHDN_N", (UA, "14")), # chip A ~SHUTDOWN, pulled low
    17: ("B_FORCEOFF_N", (UB, "22")),  # chip B ~FORCEOFF, pulled low
    19: ("A_R1OUTB", (UA, "16")), # chip A always-on non-inverting R1 (pin 2)
    20: ("A_STATUS", (UA, "15")), # chip A STATUS, valid in shutdown
    21: ("B_INVALID", (UB, "21")),# chip B INVALID, valid in shutdown
}
GPIO_PIN = {n: str(n + 2) if n <= 7 else None for n in range(30)}
GPIO_PIN.update({8: "11", 9: "12", 10: "13", 11: "14", 12: "15", 13: "16", 14: "17", 15: "18",
                 16: "27", 17: "28", 18: "29", 19: "30", 20: "31", 21: "32", 22: "34", 23: "35",
                 24: "36", 25: "37", 26: "38", 27: "39", 28: "40", 29: "41"})
for g, (net, node) in GPIO.items():
    conn(net, (U1, GPIO_PIN[g]), node)
# GP18 drives chip A ONLINE and chip B FORCEON together
conn("RS_FORCEON", (U1, GPIO_PIN[18]), (UA, "13"), (UB, "23"))
passive("R", "100k", "A_SHDN_N", GND, "chip A off until firmware decides")
passive("R", "100k", "B_FORCEOFF_N", GND, "chip B off until firmware decides")
passive("R", "100k", "RS_FORCEON", GND, "auto power-down on until firmware decides")
nc(UB, "20")  # ROUT2B unused (INVALID covers detection)
conn("VBUS_SENSE", (U1, GPIO_PIN[29]))

# --- §4.8 optional LEDs (not fitted by default) ---------------------------------
for g, name in zip(range(22, 28), ("PWR", "TR", "CD", "SD", "RD", "RI")):
    led = part("D", f"LED_{name}", DIODE, "LED_SMD:LED_0603_1608Metric", "C2286", dnp=True,
               desc=f"{name} status LED, optional")
    conn(f"LED_{name}", (U1, GPIO_PIN[g]))
    passive("R", "1k", f"LED_{name}", f"LED_{name}_A", f"{name} LED current", dnp=True)
    conn(f"LED_{name}_A", (led, "2")); conn(GND, (led, "1"))

# --- test pads ----------------------------------------------------------------
conn("GP28", (U1, GPIO_PIN[28]))
for net, label in ((VBUS, "VBUS"), (V33, "3V3"), (V11, "1V1"), (GND, "GND"), ("SWCLK", "SWCLK"),
                   ("SWDIO", "SWDIO"), ("RUN", "RUN"), ("GP28", "GP28"), ("A_TX", "A_TX"),
                   ("A_RX", "A_RX"), ("B_TX", "B_TX"), ("B_RX", "B_RX")):
    tp(net, label)


# ===========================================================================
# Checks
# ===========================================================================
def pin_type(ref, pin):
    return parts[ref]["pins"][pin][1]


def check():
    errs, warns = [], []
    on_net = {n for members in nets.values() for n in members}
    for ref, p in parts.items():
        for pin, (name, ty) in p["pins"].items():
            if (ref, pin) not in on_net and (ref, pin) not in noconnect:
                errs.append(f"{ref}.{pin} ({name}) is not connected and not marked NC")
            if (ref, pin) in on_net and (ref, pin) in noconnect:
                errs.append(f"{ref}.{pin} ({name}) is marked NC but on a net")
            if ty == NC and (ref, pin) in on_net:
                errs.append(f"{ref}.{pin} is a no-connect pin but on a net")
    for net, members in nets.items():
        refs = {r for r, _ in members}
        if len(members) < 2:
            errs.append(f"net {net} has only one pin: {members}")
        elif len(refs) < 2 and net not in SUPPLIES:
            warns.append(f"net {net} only joins pins of {refs.pop()}")
        drivers = [f"{r}.{p}" for r, p in members if pin_type(r, p) in (O, PO)]
        if len(drivers) > 1:
            errs.append(f"net {net} has {len(drivers)} outputs: {drivers}")
        for r, p in members:
            if pin_type(r, p) == PI and net not in SUPPLIES:
                errs.append(f"{r}.{p} is a power input on non-supply net {net}")
    for net, *_ in RS232:
        kinds = sorted(pin_type(r, p) for r, p in nets[net] if r in (UA, UB))
        if kinds != [I, O]:
            errs.append(f"RS-232 line {net} needs one driver and one receiver, has {kinds}")
    return errs, warns


def parse_sym(libdir, lib, name, seen=()):
    path = os.path.join(libdir, f"{lib}.kicad_symdir", f"{name}.kicad_sym")
    t = open(path).read()
    m = re.search(r'\(extends "([^"]+)"\)', t)
    if m:
        return parse_sym(libdir, lib, m.group(1))
    return {m.group(3): m.group(2) for m in
            re.finditer(r'\(pin (\w+) \w+.*?\(name "([^"]*)".*?\(number "([^"]*)"', t, re.S)}


def verify_libs(symdir, fpdir):
    errs = []
    done = set()
    for ref, p in parts.items():
        key = (id(p["pins"]), p["footprint"])
        if key in done:
            continue
        done.add(key)
        if symdir:
            sym = SYMBOLS.get(id(p["pins"]))
            if sym:
                lib_pins = parse_sym(symdir, *sym)
                if set(lib_pins) != set(p["pins"]):
                    errs.append(f"{ref}: pin numbers differ from {sym}: {sorted(set(lib_pins) ^ set(p['pins']))}")
                for num, (name, _) in p["pins"].items():
                    if num in lib_pins and lib_pins[num] not in (name, "~", "") and name != "~":
                        errs.append(f"{ref}.{num}: name {name!r} but KiCad {sym[1]} says {lib_pins[num]!r}")
        if fpdir:
            lib, fp = p["footprint"].split(":")
            path = os.path.join(fpdir, f"{lib}.pretty", f"{fp}.kicad_mod")
            if not os.path.exists(path):
                errs.append(f"{ref}: footprint {p['footprint']} not found")
                continue
            pads = set(re.findall(r'\(pad "([^"]+)"', open(path).read()))
            missing = set(p["pins"]) - pads
            if missing:
                errs.append(f"{ref}: footprint {fp} has no pad for pins {sorted(missing)}")
            extra = pads - set(p["pins"])
            if extra:
                errs.append(f"{ref}: footprint {fp} has pads with no pin: {sorted(extra)}")
    return errs


# ===========================================================================
# Output
# ===========================================================================
def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def natural(s):
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", s)]


def write_kicad(path):
    ns = uuid.UUID("6a1f6c1e-6f1d-4a43-9a52-0c0de5e2a1aa")
    out = ["(export (version \"E\")",
           f"  (design (source \"build_netlist.py\") (date {q(datetime.date.today().isoformat())}) "
           "(tool \"pico-serial build_netlist.py\"))",
           "  (components"]
    for ref in sorted(parts, key=natural):
        p = parts[ref]
        sym = SYMBOLS.get(id(p["pins"])) or ("pico-serial", p["value"])
        if p["pins"] is TWO:
            sym = ("Device", "R" if ref.startswith("R") else "C" if ref.startswith("C") else "Polyfuse")
        if p["pins"] is DIODE:
            sym = ("Device", "LED" if p["value"].startswith("LED") else "D_Zener")
        fields = [f"(field (name \"LCSC\") {q(p['lcsc'])})"] if p["lcsc"] else []
        if p["dnp"]:
            fields.append("(field (name \"DNP\") \"DNP\")")
        if p["desc"]:
            fields.append(f"(field (name \"Description\") {q(p['desc'])})")
        out.append(f"    (comp (ref {q(ref)}) (value {q(p['value'])}) (footprint {q(p['footprint'])})"
                   + (f" (fields {' '.join(fields)})" if fields else "")
                   + f" (libsource (lib {q(sym[0])}) (part {q(sym[1])}) (description \"\"))"
                   + " (sheetpath (names \"/\") (tstamps \"/\"))"
                   + f" (tstamps {q(uuid.uuid5(ns, ref))}))")
    out.append("  )")
    out.append("  (nets")
    for code, net in enumerate(sorted(nets, key=natural), 1):
        out.append(f"    (net (code {q(code)}) (name {q('/' + net if net not in SUPPLIES else net)})")
        for r, pin in sorted(nets[net], key=lambda m: (natural(m[0]), natural(m[1]))):
            name, ty = parts[r]["pins"][pin]
            out.append(f"      (node (ref {q(r)}) (pin {q(pin)}) (pinfunction {q(name)}) (pintype {q(ty)}))")
        out.append("    )")
    out.append("  )")
    out.append(")")
    open(path, "w").write("\n".join(out) + "\n")


def write_md(path):
    L = ["# pico-serial netlist", "",
         "Generated by `build_netlist.py`; edit that script, not this file.", "",
         f"{len(parts)} parts, {len(nets)} nets. DNP = not fitted by default.", "",
         "## Nets", "", "| Net | Pins |", "|---|---|"]
    for net in sorted(nets, key=natural):
        pins = ", ".join(f"{r}.{p} ({parts[r]['pins'][p][0]})" if parts[r]["pins"] is not TWO else f"{r}.{p}"
                         for r, p in sorted(nets[net], key=lambda m: (natural(m[0]), natural(m[1]))))
        L.append(f"| `{net}` | {pins} |")
    L += ["", "## Parts", "", "| Ref | Value | LCSC | Footprint | Note |", "|---|---|---|---|---|"]
    for ref in sorted(parts, key=natural):
        p = parts[ref]
        note = "; ".join(x for x in (p["desc"], p["note"], "DNP" if p["dnp"] else "",
                                     "hand-soldered" if p["assembly"] == "hand" else "") if x)
        L.append(f"| {ref} | {p['value']} | {p['lcsc']} | `{p['footprint']}` | {note} |")
    L += ["", "## Not connected (on purpose)", ""]
    for ref, pin in sorted(noconnect, key=lambda m: (natural(m[0]), natural(m[1]))):
        L.append(f"- {ref}.{pin} ({parts[ref]['pins'][pin][0]})")
    open(path, "w").write("\n".join(L) + "\n")


def write_bom(path):
    groups = defaultdict(list)
    for ref, p in parts.items():
        if p["dnp"] or p["assembly"] != "jlc":
            continue
        groups[(p["value"], p["footprint"], p["lcsc"])].append(ref)
    L = ["Comment,Designator,Footprint,LCSC Part #"]
    for (value, fp, lcsc), refs in sorted(groups.items(), key=lambda kv: natural(sorted(kv[1], key=natural)[0])):
        L.append(f"{value},\"{','.join(sorted(refs, key=natural))}\",{fp.split(':')[1]},{lcsc}")
    open(path, "w").write("\n".join(L) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--kicad-sym", help="kicad-symbols checkout, to verify pin tables")
    ap.add_argument("--kicad-fp", help="kicad-footprints checkout, to verify footprints")
    a = ap.parse_args()
    errs, warns = check()
    if a.kicad_sym or a.kicad_fp:
        errs += verify_libs(a.kicad_sym, a.kicad_fp)
    for w in warns:
        print("warning:", w)
    for e in errs:
        print("ERROR:", e)
    if errs:
        sys.exit(1)
    here = os.path.dirname(os.path.abspath(__file__))
    write_kicad(os.path.join(here, "pico-serial.net"))
    write_md(os.path.join(here, "NETLIST.md"))
    write_bom(os.path.join(here, "bom-jlc.csv"))
    fitted = sum(1 for p in parts.values() if not p["dnp"])
    print(f"ok: {len(parts)} parts ({fitted} fitted), {len(nets)} nets, "
          f"{sum(len(m) for m in nets.values())} connections, {len(noconnect)} pins left open on purpose")


if __name__ == "__main__":
    main()
