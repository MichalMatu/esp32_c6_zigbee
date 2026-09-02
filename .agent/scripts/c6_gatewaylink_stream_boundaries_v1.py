#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

FUNCTIONS = r'''
static void test_empty_delimiters_are_ignored(void)
{
    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_CRC_MISMATCH;

    for (size_t i = 0U; i < 8U; ++i) {
        assert(gateway_link_stream_feed(&decoder, 0U, &frame, &result) == GATEWAY_LINK_STREAM_NONE);
        assert(decoder.length == 0U);
        assert(!decoder.overflow);
    }

    uint8_t good[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t good_length = make_ping(20U, 21U, good);
    assert(feed(&decoder, good, good_length, &frame, &result) == GATEWAY_LINK_STREAM_FRAME);
    assert(frame.sequence == 20U);
}

static void test_truncated_frame_then_resync(void)
{
    uint8_t truncated[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t truncated_length = make_ping(30U, 31U, truncated);
    assert(truncated_length > 3U);

    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;

    assert(feed(&decoder, truncated, truncated_length - 2U, &frame, &result) == GATEWAY_LINK_STREAM_NONE);
    assert(gateway_link_stream_feed(&decoder, 0U, &frame, &result) == GATEWAY_LINK_STREAM_DROPPED);
    assert(result != GATEWAY_LINK_OK);
    assert(decoder.length == 0U);
    assert(!decoder.overflow);

    uint8_t good[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t good_length = make_ping(32U, 33U, good);
    assert(feed(&decoder, good, good_length, &frame, &result) == GATEWAY_LINK_STREAM_FRAME);
    assert(frame.sequence == 32U);
}

static void test_exact_overflow_threshold_and_resync(void)
{
    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;

    for (size_t i = 0U; i < GATEWAY_LINK_MAX_FRAME_BYTES; ++i) {
        assert(gateway_link_stream_feed(&decoder, 0x7fU, &frame, &result) == GATEWAY_LINK_STREAM_NONE);
    }
    assert(decoder.length == GATEWAY_LINK_MAX_FRAME_BYTES);
    assert(!decoder.overflow);

    assert(gateway_link_stream_feed(&decoder, 0x7fU, &frame, &result) == GATEWAY_LINK_STREAM_NONE);
    assert(decoder.length == GATEWAY_LINK_MAX_FRAME_BYTES);
    assert(decoder.overflow);
    assert(gateway_link_stream_feed(&decoder, 0U, &frame, &result) == GATEWAY_LINK_STREAM_DROPPED);
    assert(result == GATEWAY_LINK_BUFFER_TOO_SMALL);
    assert(decoder.length == 0U);
    assert(!decoder.overflow);

    uint8_t good[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t good_length = make_ping(40U, 41U, good);
    assert(feed(&decoder, good, good_length, &frame, &result) == GATEWAY_LINK_STREAM_FRAME);
    assert(frame.sequence == 40U);
}

static void test_invalid_arguments_drop_without_state_corruption(void)
{
    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;

    gateway_link_stream_init(NULL);
    assert(gateway_link_stream_feed(NULL, 0x11U, &frame, &result) == GATEWAY_LINK_STREAM_DROPPED);
    assert(gateway_link_stream_feed(&decoder, 0x11U, NULL, &result) == GATEWAY_LINK_STREAM_DROPPED);
    assert(gateway_link_stream_feed(&decoder, 0x11U, &frame, NULL) == GATEWAY_LINK_STREAM_DROPPED);
    assert(decoder.length == 0U);
    assert(!decoder.overflow);

    uint8_t good[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t good_length = make_ping(50U, 51U, good);
    assert(feed(&decoder, good, good_length, &frame, &result) == GATEWAY_LINK_STREAM_FRAME);
    assert(frame.sequence == 50U);
}
'''

CALLS = '''    test_empty_delimiters_are_ignored();\n    test_truncated_frame_then_resync();\n    test_exact_overflow_threshold_and_resync();\n    test_invalid_arguments_drop_without_state_corruption();\n'''


def transform(text: str) -> str:
    if "test_exact_overflow_threshold_and_resync" in text:
        return text
    marker = "\nint main(void)\n"
    if marker not in text:
        raise SystemExit("main marker not found")
    text = text.replace(marker, "\n" + FUNCTIONS + marker, 1)
    call_marker = "    test_overflow_recovers_on_delimiter();\n"
    if call_marker not in text:
        raise SystemExit("existing stream test call marker not found")
    return text.replace(call_marker, call_marker + CALLS, 1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--in-place", action="store_true")
    parser.add_argument("--output")
    args = parser.parse_args()

    source = Path("tests/host/test_gateway_link_stream.c")
    transformed = transform(source.read_text(encoding="utf-8"))

    if args.in_place:
        source.write_text(transformed, encoding="utf-8")
        return
    if not args.output:
        raise SystemExit("--output is required without --in-place")
    Path(args.output).write_text(transformed, encoding="utf-8")


if __name__ == "__main__":
    main()
