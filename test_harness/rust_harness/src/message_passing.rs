
#[derive(Default, Debug)]
struct StateEmulator {
    current_state: u8,
    elapsed_time: u16,
}

fn main() {
    let state = Arc::new(RwLock::new(StateEmulator::default()));

    let writer_state = Arc::clone(&state);
    let reader_state = Arc::clone(&state);

    // Thread 1: UART receiver updates the state
    let writer = thread::spawn(move || {
        loop {
            {
                let mut state = writer_state.write().unwrap();
                state.current_state = 1;
                state.elapsed_time += 1;
            }

            thread::sleep(Duration::from_secs(1));
        }
    });

    // Thread 2: Reads the state
    let reader = thread::spawn(move || {
        loop {
            {
                let state = reader_state.read().unwrap();

                println!("State: {}", state.current_state);
                println!("Elapsed: {}", state.elapsed_time);
            }

            thread::sleep(Duration::from_millis(100));
        }
    });

    writer.join().unwrap();
    reader.join().unwrap();
}