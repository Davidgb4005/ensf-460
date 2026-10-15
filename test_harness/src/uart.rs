
use std::io::{self};
use std::collections::VecDeque;
use serialport::{SerialPort};
use crate::state::{TwinState,TimerStates,LedState,SwitchState};

#[derive(Default)]
pub(crate) struct UartReader {
    packet: Vec<u8>,
    previous_byte: u8,
    decoded: VecDeque<TwinState>,
}

impl UartReader {
    pub(crate) fn poll(&mut self, port: &mut dyn SerialPort) -> io::Result<Option<TwinState>> {
        if let Some(state) = self.decoded.pop_front() {
            return Ok(Some(state));
        }

        let mut buffer = [0u8; 128];
        match port.read(&mut buffer) {
            Ok(byte_count) => {
                for &byte in &buffer[..byte_count] {
                    self.consume(byte);
                }
                Ok(self.decoded.pop_front())
            }
            Err(error) if error.kind() == io::ErrorKind::TimedOut => Ok(None),
            Err(error) => Err(error),
        }
    }

    pub(crate) fn consume(&mut self, byte: u8) {
        if self.packet.is_empty() {
            if self.previous_byte == 0xAA && byte == 0x55 {
                self.packet.extend_from_slice(&[0xAA, 0x55]);
            }
        } else {
            self.packet.push(byte);
            if self.packet.len() == 10 {
                let packet: [u8; 10] = self.packet.as_slice().try_into().unwrap();
                if let Some(state) = decode_packet(&packet) {
                    self.decoded.push_back(state);
                }
                self.packet.clear();
            }
        }
        self.previous_byte = byte;
    }
}

fn decode_packet(packet: &[u8; 10]) -> Option<TwinState> {
    if packet[0] != 0xAA || packet[1] != 0x55 {
        return None;
    }
    let checksum = packet[2..9]
        .iter()
        .fold(0u8, |sum, byte| sum.wrapping_add(*byte));
    if checksum != packet[9] {
        return None;
    }

    let gpio_flags = packet[6];
    let state_flags = u16::from_le_bytes([packet[7], packet[8]]);
    Some(TwinState {
        current_state: TimerStates::try_from((state_flags & 0x1f) as u8).ok()?,
        prev_state: TimerStates::try_from(((state_flags >> 5) & 0x1f) as u8).ok()?,
        led_1_state: LedState::try_from(gpio_flags & 0x03).ok()?,
        led_2_state: LedState::try_from((gpio_flags >> 2) & 0x03).ok()?,
        pb_1_state: SwitchState::try_from((gpio_flags >> 4) & 0x01).ok()?,
        pb_2_state: SwitchState::try_from((gpio_flags >> 5) & 0x01).ok()?,
        pb_3_state: SwitchState::try_from((gpio_flags >> 6) & 0x01).ok()?,
        elapsed_time: u16::from_le_bytes([packet[2], packet[3]]),
        preset_time: u16::from_le_bytes([packet[4], packet[5]]),
    })
}