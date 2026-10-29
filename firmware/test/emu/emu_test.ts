// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 D-Modem Authors
// End-to-end test of the real firmware image in the rp2040js emulator:
// boot, USB enumeration (the emulator plays the host), DCE/DTE detection via
// the transceivers' status lines, modem-line mapping on the GPIOs, data both
// ways through the UART, CDC SERIAL_STATE notifications, vendor requests, and
// learning the baud rate from characters driven onto the RX pin (GP1).
//
//   firmware/test/emu/run.sh <firmware .bin>
import { readFileSync } from 'fs';
import {
  Simulator, USBCDC, createSetupPacket, DataDirection, SetupType, SetupRecipient, ConsoleLogger, LogLevel,
} from 'rp2040js';
import { bootromB1 } from './vendor/bootrom.js';

// GPIO numbers (firmware/src/generated/board_pins.h)
const P = {
  A_RTS: 3, A_DSR: 4, A_DCD: 5, A_RI: 6, A_DTR: 7, B_DTR: 8, B_DSR: 9, B_DCD: 10, B_RI: 11,
  B_RTS: 15, A_SHDN_N: 16, B_FORCEOFF_N: 17, RS_FORCEON: 18, A_R1OUTB: 19, A_STATUS: 20, B_INVALID: 21,
};
const ITF_CONTROL = 2;
const REQ = { GET_INFO: 1, SET_MODE: 2, GET_INPUTS: 4, AUTOBAUD_RESTART: 9 };
const INFO = { LEN: 14, BAUD_LOCKED: 0x08, BAUD_LEARNING: 0x10 };
const A_RX = 1;

let failures = 0, checks = 0;
// Counts a check and prints it as ok or FAIL.
function check(cond: boolean, what: string) {
  checks++;
  if (!cond) { failures++; console.log(`  FAIL: ${what}`); } else console.log(`  ok: ${what}`);
}

const image = process.argv[2];
const sim = new Simulator();
const mcu = sim.rp2040;
mcu.logger = new ConsoleLogger(LogLevel.Error);
mcu.loadBootrom(bootromB1);
mcu.flash.set(readFileSync(image), 0);
// The emulated RP2040's GPIO n.
const pin = (n: number) => mcu.gpio[n];
// The level the firmware drives on GPIO n.
const out = (n: number) => pin(n).outputValue;

// the far end: nothing attached yet, every receiver output idle (high = deasserted)
for (const n of [P.A_STATUS, P.B_INVALID]) pin(n).setInputValue(false);
for (const n of [P.A_DTR, P.B_DSR, P.B_DCD, P.B_RI, 1, 2, 13, 14]) pin(n).setInputValue(true);

const uartOut: number[] = [];
mcu.uart[0].onByte = (b) => uartOut.push(b);

const cdc = new USBCDC(mcu.usbCtrl);
let connected = false;
const hostIn: number[] = [];
cdc.onDeviceConnected = () => { connected = true; };
cdc.onSerialData = (buf) => hostIn.push(...buf);

// Watch endpoint writes ourselves too: notifications (EP1) and our own control reads (EP0).
const notifications: number[][] = [];
let ep0Waiter: ((b: Uint8Array) => void) | null = null;
const cdcWrite = mcu.usbCtrl.onEndpointWrite!;
mcu.usbCtrl.onEndpointWrite = (ep, buf) => {
  if (ep === 1 && buf.length >= 10) { notifications.push([...buf]); return; }
  if (ep === 0 && ep0Waiter) {
    if (buf.length === 0) return;   // status stage of an OUT request
    const w = ep0Waiter; ep0Waiter = null; w(buf); return;
  }
  cdcWrite(ep, buf);
};

// rp2040js runs PIO on its own timer, not the simulated clock. The baud-rate
// learning times the RX line with PIO, so step it once per CPU cycle instead
// (the firmware runs the system clock at 125 MHz, as the emulator does).
const pio0 = mcu.pio[0] as any;
pio0.run = () => {};
const simAny = sim as any;
sim.execute = () => {
  simAny.executeTimer = null;
  simAny.stopped = false;
  for (let i = 0; i < 200000 && !simAny.stopped; i++) {
    let cycles: number;
    if (mcu.core.waiting) {
      const n = sim.clock.nanosToNextAlarm;
      sim.clock.tick(n);
      cycles = Math.min(Math.ceil(n / 8), 1 << 20);
    } else {
      cycles = mcu.core.executeInstruction();
      sim.clock.tick(cycles * 8);
    }
    if (!pio0.stopped) for (let k = 0; k < cycles; k++) pio0.step();
  }
  if (!simAny.stopped) simAny.executeTimer = setTimeout(() => sim.execute(), 0);
};

// A computer typing: drive characters (8N1) onto a receiver output pin, on
// the simulated clock. Receiver outputs are inverted RS-232: idle = high.
function sendWave(gpio: number, baud: number, text: string, errPct = 0): Promise<void> {
  const levels: boolean[] = [];
  for (const ch of Buffer.from(text)) {
    levels.push(false);
    for (let d = 0; d < 8; d++) levels.push(((ch >> d) & 1) === 1);
    levels.push(true, true);   // stop bit and a little idle
  }
  const bitNs = 1e9 / (baud * (1 + errPct / 100));
  return new Promise((resolve) => {
    let k = 0;
    const alarm = sim.clock.createAlarm(() => {
      pin(gpio).setInputValue(levels[k]);
      k++;
      if (k < levels.length) alarm.schedule(bitNs);
      else resolve();
    });
    alarm.schedule(1000);
  });
}

sim.rp2040.core.PC = 0x10000000;
sim.execute();

// Simulated time, in ms.
const simMs = () => Number(sim.clock.nanos / 1e6);
// Waits (checking every 5 ms of real time) until cond holds or simLimitMs of
// simulated time pass; true if cond held.
async function until(cond: () => boolean, simLimitMs: number): Promise<boolean> {
  const end = simMs() + simLimitMs;
  while (!cond()) {
    if (simMs() > end) return false;
    await new Promise((r) => setTimeout(r, 5));
  }
  return true;
}
// Lets ms of simulated time pass.
const runFor = (ms: number) => until(() => false, ms);

// Sends a vendor IN request to the control interface and resolves with the
// reply's data stage (up to len bytes), or null after 20 s of real time.
function vendorIn(req: number, len: number): Promise<Uint8Array | null> {
  return new Promise((resolve) => {
    const timer = setTimeout(() => { ep0Waiter = null; resolve(null); }, 20000);
    ep0Waiter = (b) => { clearTimeout(timer); resolve(b); };
    mcu.usbCtrl.sendSetupPacket(createSetupPacket({
      dataDirection: DataDirection.DeviceToHost, type: SetupType.Vendor, recipient: SetupRecipient.Interface,
      bRequest: req, wValue: 0, wIndex: ITF_CONTROL, wLength: len,
    }));
  });
}
// Sends a vendor OUT request (no data stage) to the control interface.
function vendorOut(req: number, value: number) {
  ep0Waiter = () => {};   // swallow the status stage
  mcu.usbCtrl.sendSetupPacket(createSetupPacket({
    dataDirection: DataDirection.HostToDevice, type: SetupType.Vendor, recipient: SetupRecipient.Interface,
    bRequest: req, wValue: value, wIndex: ITF_CONTROL, wLength: 0,
  }));
}
// The line-state bits of the last SERIAL_STATE notification, or -1 if none came.
const lastState = () => {
  const n = notifications[notifications.length - 1];
  return n ? n[8] | (n[9] << 8) : -1;
};

// The end-to-end scenario: enumerate over USB, detect a computer, learn its
// baud rate, pass data both ways, answer vendor requests, learn a new rate,
// and force DTE mode; exits 1 if any check failed.
async function main() {
  console.log('boot and enumerate');
  check(await until(() => connected, 3000), 'USB enumerates as a CDC device and takes DTR+RTS');
  await runFor(300);
  check(!out(P.A_SHDN_N) && !out(P.B_FORCEOFF_N), 'nothing attached: both transceivers shut down');

  console.log('a computer appears on the DB25 (chip A STATUS goes high)');
  pin(P.A_STATUS).setInputValue(true);
  check(await until(() => out(P.A_SHDN_N), 1000), 'chip A (DCE) switched on');
  check(!out(P.B_FORCEOFF_N) && out(P.RS_FORCEON), 'chip B stays off; auto power-down disabled');
  await runFor(50);
  // host set DTR+RTS: as DCE that's DCD+DSR, and CTS (driven low = asserted through the inverting driver)
  check(!out(P.A_DCD) && !out(P.A_DSR), "host DTR asserts the computer's DCD and DSR");
  check(!out(P.A_RTS), "host RTS asserts the computer's CTS");
  check(out(P.A_RI), 'RI stays off');

  console.log('learning the baud rate: the computer types "AT\\r" at 9600 baud (1% fast)');
  const learning = await vendorIn(REQ.GET_INFO, INFO.LEN);
  check(!!learning && learning.length === INFO.LEN && (learning[8] & INFO.BAUD_LEARNING) !== 0,
    'GET_INFO (14 bytes) says it is learning the baud rate');
  mcu.uart[0].feedByte(0x78);   // what the UART makes of it at the wrong rate
  await runFor(20);
  check(hostIn.length === 0, 'while learning, the UART\'s bytes are held back');
  await sendWave(A_RX, 9600, 'AT\r', 1);
  check(await until(() => hostIn.length >= 3, 500) && Buffer.from(hostIn).toString() === 'AT\r',
    'the characters it learned from reach the host: "AT\\r"');
  hostIn.length = 0;
  const locked = await vendorIn(REQ.GET_INFO, INFO.LEN);
  if (locked) {
    const dv = new DataView(locked.buffer, locked.byteOffset);
    check(dv.getUint32(9, true) === 9600, `UART now at 9600 (got ${dv.getUint32(9, true)})`);
    check((locked[8] & (INFO.BAUD_LOCKED | INFO.BAUD_LEARNING)) === INFO.BAUD_LOCKED, 'flags: baud locked, not learning');
    check(locked[13] === 0, 'baud mode AUTO');
  } else check(false, 'GET_INFO answers');

  console.log('data both ways');
  for (const ch of Buffer.from('ATZ\r')) cdc.sendSerialByte(ch);
  check(await until(() => uartOut.length >= 4, 500) && Buffer.from(uartOut).toString() === 'ATZ\r',
    'host -> RS-232: "ATZ\\r" leaves the UART');
  for (const ch of Buffer.from('OK\r\n')) mcu.uart[0].feedByte(ch);
  check(await until(() => hostIn.length >= 4, 500) && Buffer.from(hostIn).toString() === 'OK\r\n',
    'RS-232 -> host: "OK\\r\\n" arrives over USB');

  console.log("the computer raises DTR");
  pin(P.A_DTR).setInputValue(false);   // asserted reads low through the inverting receiver
  check(await until(() => (lastState() & 0x03) === 0x03, 500), 'SERIAL_STATE reports DCD (computer attached) + DSR (its DTR)');

  console.log('vendor requests');
  const info = await vendorIn(REQ.GET_INFO, INFO.LEN);
  check(!!info && info.length === INFO.LEN, 'GET_INFO answers 14 bytes');
  if (info) {
    const dv = new DataView(info.buffer, info.byteOffset);
    check(info[0] === 1, `protocol version 1 (got ${info[0]})`);
    check(info[6] === 1, `role DCE (got ${info[6]})`);
    check(dv.getUint32(9, true) === 9600, `baud still the learned 9600 (got ${dv.getUint32(9, true)})`);
  }
  const inputs = await vendorIn(REQ.GET_INPUTS, 5);
  check(!!inputs && (inputs[1] & 0x01) === 0x01 && (inputs[3] & 0x01) === 0x01,
    "GET_INPUTS shows the computer's DTR and chip A STATUS");

  console.log('learn again (vendor request): now 115200 baud, 2% slow');
  vendorOut(REQ.AUTOBAUD_RESTART, 0);
  await runFor(20);
  hostIn.length = 0;
  await sendWave(A_RX, 115200, 'ATI3\r', -2);
  check(await until(() => hostIn.length >= 5, 500) && Buffer.from(hostIn).toString() === 'ATI3\r',
    '"ATI3\\r" reaches the host');
  const again = await vendorIn(REQ.GET_INFO, INFO.LEN);
  check(!!again && new DataView(again.buffer, again.byteOffset).getUint32(9, true) === 115200, 'UART now at 115200');

  console.log('force DTE mode');
  vendorOut(REQ.SET_MODE, 2);
  check(await until(() => !out(P.A_SHDN_N) && out(P.B_FORCEOFF_N), 500), 'chip A off, chip B (DTE) on');
  await runFor(50);
  check(!out(P.B_DTR) && !out(P.B_RTS), "host DTR and RTS drive the modem's DTR and RTS");
  check(await until(() => (lastState() & 0x03) === 0, 500), 'modem not yet signalling: SERIAL_STATE clears');
  pin(P.B_DCD).setInputValue(false);
  pin(P.B_DSR).setInputValue(false);
  check(await until(() => (lastState() & 0x03) === 0x03, 500), "modem's DCD and DSR reach the host");
  // rp2040js doesn't emulate flash erase/program, so the write itself can't be
  // checked here; check the firmware gets through the save (500 ms after the
  // change) without hanging or a watchdog reset, still in DTE mode.
  await runFor(1500);
  const after = await vendorIn(REQ.GET_INFO, INFO.LEN);
  check(!!after && after[5] === 2 && after[6] === 2, 'still running in DTE mode after the settings save');
  if (after) {
    const dv = new DataView(after.buffer, after.byteOffset);
    check((after[8] & (INFO.BAUD_LOCKED | INFO.BAUD_LEARNING)) === 0 && dv.getUint32(9, true) === 115200,
      "as DTE the host's baud rate (115200) is used, not learned");
  }

  sim.stop();
  console.log(`${checks} checks, ${failures} failed`);
  process.exit(failures ? 1 : 0);
}

main().catch((e) => { console.error(e); process.exit(2); });
