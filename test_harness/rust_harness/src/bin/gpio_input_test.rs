use rppal::gpio::Gpio;
use std::error::Error;
use std::thread;
use std::time::Duration;

const PB1_SAMPLE_PIN: u8 = 18;
const PB2_SAMPLE_PIN: u8 = 23;
const PB3_SAMPLE_PIN: u8 = 24;
const LED1_SAMPLE_PIN: u8 = 25;
const LED2_SAMPLE_PIN: u8 = 26;

fn main() -> Result<(), Box<dyn Error>> {
    let gpio = Gpio::new()?;

    let pb1 = gpio.get(PB1_SAMPLE_PIN)?.into_input_pulldown();
    let pb2 = gpio.get(PB2_SAMPLE_PIN)?.into_input_pulldown();
    let pb3 = gpio.get(PB3_SAMPLE_PIN)?.into_input_pulldown();
    let led1 = gpio.get(LED1_SAMPLE_PIN)?.into_input();
    let led2 = gpio.get(LED2_SAMPLE_PIN)?.into_input();

    println!("Monitoring GPIO inputs. Press Ctrl+C to stop.");
    println!("PB1=BCM18 PB2=BCM23 PB3=BCM24 LED1=BCM25 LED2=BCM26");

    let mut previous = None;

    loop {
        let current = [
            pb1.is_high(),
            pb2.is_high(),
            pb3.is_high(),
            led1.is_high(),
            led2.is_high(),
        ];

        if previous != Some(current) {
            println!(
                "PB1={} PB2={} PB3={} LED1={} LED2={}",
                current[0] as u8,
                current[1] as u8,
                current[2] as u8,
                current[3] as u8,
                current[4] as u8,
            );
            previous = Some(current);
        }

        thread::sleep(Duration::from_millis(10));
    }
}
