import unittest

from testing.twin.protocol import (
    FRAME_MARKER,
    FRAME_SIZE,
    FrameDecoder,
    StatusFrame,
    decode_frame,
    encode_frame,
)


def sample_frame(**overrides):
    values = {
        "elapsed_seconds": 58,
        "preset_seconds": 60,
        "state_code": 1,
        "previous_state_code": 0,
        "pb1": True,
        "pb2": False,
        "pb3": True,
        "led1_state_code": 3,
        "led2_state_code": 2,
    }
    values.update(overrides)
    return StatusFrame(**values)


class ProtocolTests(unittest.TestCase):
    def test_round_trip_uses_nine_byte_little_endian_frame(self):
        original = sample_frame()
        encoded = encode_frame(original)

        self.assertEqual(len(encoded), FRAME_SIZE)
        self.assertEqual(encoded[:2], FRAME_MARKER)
        self.assertEqual(encoded[2:4], b"\x3a\x00")
        self.assertEqual(decode_frame(encoded), original)

    def test_checksum_failure_is_rejected(self):
        encoded = bytearray(encode_frame(sample_frame()))
        encoded[4] ^= 1

        with self.assertRaisesRegex(ValueError, "checksum"):
            decode_frame(bytes(encoded))

    def test_decodes_firmware_example(self):
        frame = decode_frame(bytes((170, 85, 0, 0, 4, 0, 11, 0, 15)))

        self.assertEqual(frame.elapsed_seconds, 0)
        self.assertEqual(frame.preset_seconds, 4)
        self.assertEqual(frame.state, "ALARM")
        self.assertEqual(frame.previous_state, "RUN")

    def test_stream_decoder_handles_partial_and_multiple_frames(self):
        first = encode_frame(sample_frame(elapsed_seconds=1))
        second = encode_frame(sample_frame(elapsed_seconds=2))
        decoder = FrameDecoder()

        self.assertEqual(decoder.feed(first[:4]), ())
        frames = decoder.feed(first[4:] + second)

        self.assertEqual([frame.elapsed_seconds for frame in frames], [1, 2])

    def test_stream_decoder_recovers_from_noise_and_bad_checksum(self):
        bad = bytearray(encode_frame(sample_frame(elapsed_seconds=1)))
        bad[-1] ^= 1
        valid = encode_frame(sample_frame(elapsed_seconds=2))
        decoder = FrameDecoder()

        frames = decoder.feed(b"noise\xaa" + bytes(bad) + valid)

        self.assertEqual([frame.elapsed_seconds for frame in frames], [2])
        self.assertEqual(decoder.invalid_frames, 1)

    def test_invalid_field_ranges_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "state_code"):
            encode_frame(sample_frame(state_code=8))
        with self.assertRaisesRegex(ValueError, "elapsed_seconds"):
            encode_frame(sample_frame(elapsed_seconds=65536))


if __name__ == "__main__":
    unittest.main()
