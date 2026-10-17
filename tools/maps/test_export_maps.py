"""Driver tests run without retail data or a Godot binary."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('export_maps', Path(__file__).with_name('export_maps.py'))
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)


class ExportTests(unittest.TestCase):
    def test_selection_uses_exact_keys(self):
        maps = [{'key': 'a/shared.map'}, {'key': 'b/shared.map'}]
        self.assertEqual(exporter.select_maps(maps, ['b/shared.map'], False), maps[1:])
        self.assertEqual(exporter.map_stem(r'Maps\Shared.map'), 'shared')
        self.assertNotEqual(exporter.map_id(maps[0]['key']), exporter.map_id(maps[1]['key']))
        with self.assertRaises(ValueError):
            exporter.select_maps(maps, ['missing'], False)

    def test_batch_survives_timeout_and_escapes_gallery(self):
        entries = [dict(key=key, name='<script>alert(1)</script>', description='A & B', players=2, official=True)
                   for key in ['first', 'second']]
        def worker(godot, request, temporary, timeout, metadata_only):
            if request['mode'] == 'catalog':
                return {'ok': True, 'maps': entries, 'content_signature': 'test'}
            if request['entry']['key'] == 'first':
                raise subprocess.TimeoutExpired('godot', 1)
            result = {'ok': True, 'map': request['entry'], 'preview': {'status': 'disabled'}}
            Path(request['output']).write_text(json.dumps(result))
            return result
        with tempfile.TemporaryDirectory() as directory, patch.object(exporter, 'run_worker', side_effect=worker):
            out = Path(directory)
            stale = out / (exporter.map_id('first') + '.png')
            stale.write_bytes(b'stale')
            self.assertEqual(exporter.main(['--out', directory, '--all', '--metadata-only']), 1)
            manifest = json.loads((out / 'maps.json').read_text())
            self.assertEqual(len(manifest['maps']), 2)
            self.assertFalse(manifest['maps'][0]['ok'])
            self.assertTrue(manifest['maps'][1]['ok'])
            self.assertFalse(stale.exists())
            gallery = (out / 'index.html').read_text()
            self.assertNotIn('<script>', gallery)
            self.assertIn('&lt;script&gt;', gallery)

    def test_screenshot_failure_is_nonzero(self):
        entry = dict(key='map', name='Map', players=2, official=False)
        def worker(godot, request, temporary, timeout, metadata_only):
            if request['mode'] == 'catalog':
                return {'ok': True, 'maps': [entry], 'content_signature': 'test'}
            return {'ok': True, 'map': entry, 'preview': {'status': 'failed', 'error': 'No renderer'}}
        with tempfile.TemporaryDirectory() as directory, patch.object(exporter, 'run_worker', side_effect=worker):
            self.assertEqual(exporter.main(['--out', directory, '--all']), 1)

    def test_worker_discards_stale_output_and_reports_crash(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / 'map.json'
            out.write_text('{"ok":true}')
            process = subprocess.CompletedProcess([], 1, 'stdout', 'renderer failed')
            with patch.object(exporter.subprocess, 'run', return_value=process):
                with self.assertRaisesRegex(RuntimeError, 'renderer failed'):
                    exporter.run_worker('godot', {'output': str(out)}, Path(directory), 1, True)
            self.assertFalse(out.exists())


if __name__ == '__main__':
    unittest.main()
