from pathlib import Path

INDEX = Path('frontend/index.html')
TEST = Path('tests/frontend/pipeline-selection-buffer-edge.test.mjs')

html = INDEX.read_text(encoding='utf-8')

def insert_before_once(src: str, marker: str, insert: str, label: str) -> str:
    count = src.count(marker)
    if count != 1:
        raise SystemExit(f'{label}: expected 1 marker, found {count}')
    return src.replace(marker, insert + marker, 1)

keyboard_marker = "    window.addEventListener('resize', () => { updateWorldBounds(); if (!defaultViewportZoomApplied) applyInitialViewportZoomIfNeeded('resize_before_first_view'); renderAll(true); });\n"
keyboard_insert = """    document.addEventListener('keydown', e => {
      if (!shouldHandlePipelineSelectAllV104(e)) return;
      e.preventDefault();
      e.stopPropagation();
      selectAllVisiblePipelineLayoutV104();
    }, true);

"""
if "document.addEventListener('keydown', e => {\n      if (!shouldHandlePipelineSelectAllV104(e)) return;" not in html:
    html = insert_before_once(html, keyboard_marker, keyboard_insert, 'ctrl+a listener insert')

INDEX.write_text(html, encoding='utf-8')

test = TEST.read_text(encoding='utf-8')
listener_assert = "assert.ok(html.includes(\"document.addEventListener('keydown', e => {\"), 'Ctrl+A keydown listener should be installed');\nassert.ok(html.includes('selectAllVisiblePipelineLayoutV104();'), 'Ctrl+A keydown listener should invoke select-all helper');\n"
if "Ctrl+A keydown listener should be installed" not in test:
    test = test.replace("assert.ok(html.includes('selection_all_visible_layout'), 'select-all should report backend edit telemetry');\n", "assert.ok(html.includes('selection_all_visible_layout'), 'select-all should report backend edit telemetry');\n" + listener_assert)
TEST.write_text(test, encoding='utf-8')
print('pipeline Ctrl+A listener patch applied')
