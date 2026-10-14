use crate::{GpioOutputState, TwinState};
use std::fs::{File, OpenOptions};
use std::io::{self, BufWriter, Write};
use std::path::Path;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

pub(crate) struct EventLogger {
    writer: BufWriter<File>,
    started_at: Instant,
}

impl EventLogger {
    pub(crate) fn new(path: impl AsRef<Path>) -> io::Result<Self> {
        let file = OpenOptions::new().create(true).append(true).open(path)?;
        Ok(Self {
            writer: BufWriter::new(file),
            started_at: Instant::now(),
        })
    }

    pub(crate) fn log(
        &mut self,
        event: &str,
        uart: &TwinState,
        twin: &TwinState,
        gpio: GpioOutputState,
    ) -> io::Result<()> {
        let unix_time = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default();
        writeln!(
            self.writer,
            "[unix={}.{:03} elapsed={}]",
            unix_time.as_secs(),
            unix_time.subsec_millis(),
            elapsed_timestamp(self.started_at.elapsed()),
        )?;
        writeln!(self.writer, "event={event}")?;
        writeln!(self.writer, "uart={uart:?}")?;
        writeln!(self.writer, "twin={twin:?}")?;
        writeln!(self.writer, "gpio={gpio:?}")?;
        writeln!(self.writer)?;
        self.writer.flush()
    }
}

fn elapsed_timestamp(elapsed: Duration) -> String {
    let total_seconds = elapsed.as_secs();
    let hours = total_seconds / 3600;
    let minutes = (total_seconds % 3600) / 60;
    let seconds = total_seconds % 60;
    format!(
        "{hours:02}:{minutes:02}:{seconds:02}.{:03}",
        elapsed.subsec_millis()
    )
}
