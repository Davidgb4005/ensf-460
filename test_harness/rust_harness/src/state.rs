

#[derive(Default, Debug, Clone, PartialEq, Eq)]
pub(crate) enum LedState {
    #[default]
    Off,
    On,
    Blink,
    Pause,
}

#[derive(Default, Debug, Clone, PartialEq, Eq)]
pub(crate) enum SwitchState {
    On,
    #[default]
    Off,
}

#[derive(Default, Debug, Copy, Clone, PartialEq, Eq)]
pub(crate) enum TimerStates {
    #[default]
    InitEntry,
    Init,
    InitTransition,
    InitToReset,
    IdleEntry,
    Idle,
    IdleTransition,
    IdleToRun,
    IdleToReset,
    ResetEntry,
    Reset,
    ResetTransition,
    ResetToIdle,
    RunEntry,
    Run,
    RunTransition,
    RunToAlarm,
    RunToPause,
    RunToClear,
    PauseEntry,
    Pause,
    PauseTransition,
    PauseToRun,
    PauseToClear,
    ClearEntry,
    Clear,
    ClearTransition,
    ClearToIdle,
    AlarmEntry,
    Alarm,
    AlarmTransition,
    AlarmToIdle,
}

impl TryFrom<u8> for TimerStates {
    type Error = &'static str;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::InitEntry),
            1 => Ok(Self::Init),
            2 => Ok(Self::InitTransition),
            3 => Ok(Self::InitToReset),
            4 => Ok(Self::IdleEntry),
            5 => Ok(Self::Idle),
            6 => Ok(Self::IdleTransition),
            7 => Ok(Self::IdleToRun),
            8 => Ok(Self::IdleToReset),
            9 => Ok(Self::ResetEntry),
            10 => Ok(Self::Reset),
            11 => Ok(Self::ResetTransition),
            12 => Ok(Self::ResetToIdle),
            13 => Ok(Self::RunEntry),
            14 => Ok(Self::Run),
            15 => Ok(Self::RunTransition),
            16 => Ok(Self::RunToAlarm),
            17 => Ok(Self::RunToPause),
            18 => Ok(Self::RunToClear),
            19 => Ok(Self::PauseEntry),
            20 => Ok(Self::Pause),
            21 => Ok(Self::PauseTransition),
            22 => Ok(Self::PauseToRun),
            23 => Ok(Self::PauseToClear),
            24 => Ok(Self::ClearEntry),
            25 => Ok(Self::Clear),
            26 => Ok(Self::ClearTransition),
            27 => Ok(Self::ClearToIdle),
            28 => Ok(Self::AlarmEntry),
            29 => Ok(Self::Alarm),
            30 => Ok(Self::AlarmTransition),
            31 => Ok(Self::AlarmToIdle),
            _ => Err("invalid timer state"),
        }
    }
}

impl TryFrom<u8> for SwitchState {
    type Error = &'static str;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::Off),
            1 => Ok(Self::On),
            _ => Err("invalid switch state"),
        } 
    }
}

impl TryFrom<u8> for LedState {
    type Error = &'static str;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::On),
            1 => Ok(Self::Off),
            2 => Ok(Self::Blink),
            3 => Ok(Self::Pause),
            _ => Err("invalid LED state"),
        }
    }
}

#[derive(Default, Debug, Clone, PartialEq, Eq)]
pub(crate) struct TwinState {
    pub(crate) current_state: TimerStates,
    pub(crate) prev_state: TimerStates,
    pub(crate) led_1_state: LedState,
    pub(crate) led_2_state: LedState,
    pub(crate) pb_1_state: SwitchState,
    pub(crate) pb_2_state: SwitchState,
    pub(crate) pb_3_state: SwitchState,
    pub(crate) preset_time: u16,
    pub(crate) elapsed_time: u16,
}

#[derive(Default, Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct GpioOutputState {
    pub(crate) pb_1: bool,
    pub(crate) pb_2: bool,
    pub(crate) pb_3: bool,
    pub(crate) reset: bool,
}
