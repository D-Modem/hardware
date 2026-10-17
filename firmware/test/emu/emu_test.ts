// End-to-end test of the real firmware image in the rp2040js emulator:
// boot, USB enumeration (the emulator plays the host), DCE/DTE detection via
// the transceivers' status lines, modem-line mapping on the GPIOs, data both
// ways through the UART, CDC SERIAL_STATE notifications and vendor requests.
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
const REQ = { GET_INFO: 1, SET_MODE: 2, GET_INPUTS: 4 };

let failures = 0, checks = 0;
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
const pin = (n: number) => mcu.gpio[n];
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

sim.rp2040.core.PC = 0x10000000;
sim.execute();

const simMs = () => Number(sim.clock.nanos / 1e6);
async function until(cond: () => boolean, simLimitMs: number): Promise<boolean> {
  const end = simMs() + simLimitMs;
  while (!cond()) {
    if (simMs() > end) return false;
    await new Promise((r) => setTimeout(r, 5));
  }
  return true;
}
const runFor = (ms: number) => until(() => false, ms);

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
function vendorOut(req: number, value: number) {
  ep0Waiter = () => {};   // swallow the status stage
  mcu.usbCtrl.sendSetupPacket(createSetupPacket({
    dataDirection: DataDirection.HostToDevice, type: SetupType.Vendor, recipient: SetupRecipient.Interface,
    bRequest: req, wValue: value, wIndex: ITF_CONTROL, wLength: 0,
  }));
}
const lastState = () => {
  const n = notifications[notifications.length - 1];
  return n ? n[8] | (n[9] << 8) : -1;
};

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
  const info = await vendorIn(REQ.GET_INFO, 13);
  check(!!info && info.length === 13, 'GET_INFO answers 13 bytes');
  if (info) {
    const dv = new DataView(info.buffer, info.byteOffset);
    check(info[0] === 1, `protocol version 1 (got ${info[0]})`);
    check(info[6] === 1, `role DCE (got ${info[6]})`);
    check(dv.getUint32(9, true) === 115200, `baud 115200 (got ${dv.getUint32(9, true)})`);
  }
  const inputs = await vendorIn(REQ.GET_INPUTS, 5);
  check(!!inputs && (inputs[1] & 0x01) === 0x01 && (inputs[3] & 0x01) === 0x01,
    "GET_INPUTS shows the computer's DTR and chip A STATUS");

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
  const after = await vendorIn(REQ.GET_INFO, 13);
  check(!!after && after[5] === 2 && after[6] === 2, 'still running in DTE mode after the settings save');

  sim.stop();
  console.log(`${checks} checks, ${failures} failed`);
  process.exit(failures ? 1 : 0);
}

main().catch((e) => { console.error(e); process.exit(2); });
