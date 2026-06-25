#!/usr/bin/env python3
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOF_AUDIO_STUDIO = ROOT.parent / 'sof' / 'src' / 'audio_studio' / 'audio_studio.c'


def require_contains(text, expected):
    assert expected in text, f'missing {expected!r}'


def main():
    text = SOF_AUDIO_STUDIO.read_text(encoding='utf-8')

    assert (
        'block_size=0|free_count=0|total_count=0|used_bytes=0|free_bytes=0'
        not in text
    ), 'audio_studio heap rows must not be fixed zero-value placeholders'
    assert (
        'heap|category=%s' not in text
    ), 'SOF trace heap category must be a literal so sof_logger can decode it reliably'
    require_contains(text, 'memmap_get()')
    require_contains(text, 'trace_heap_system_rows(memmap->system')
    require_contains(text, 'trace_heap_system_runtime_rows(memmap->system_runtime')
    require_contains(text, 'trace_heap_buffer_rows(memmap->buffer')
    require_contains(text, 'trace_heap_runtime_rows(memmap->runtime')
    require_contains(text, '"heap|category=" category_name')
    require_contains(text, 'current_map->free_count')
    require_contains(text, 'current_map->count')


if __name__ == '__main__':
    main()
