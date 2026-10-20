"""Run inside SqlStudio with --run-script and --quit-when-done."""
import gc
import unittest

import grt


class GrtDictIterationTests(unittest.TestCase):
    def test_empty(self):
        self.assertEqual(list(grt.Dict()), [])

    def test_all_values(self):
        mapping = grt.Dict()
        mapping['a'] = 'first'
        mapping['b'] = 'last'
        self.assertEqual(list(mapping), ['first', 'last'])
        self.assertIn('first', mapping)
        self.assertIn('last', mapping)
        self.assertNotIn('missing', mapping)

    def test_single_value(self):
        mapping = grt.Dict()
        mapping['a'] = 'only'
        self.assertEqual(list(mapping), ['only'])

    def test_iterator_owns_snapshot(self):
        mapping = grt.Dict()
        mapping['a'] = 'original'
        iterator = iter(mapping)
        mapping['a'] = 'changed'
        del mapping
        gc.collect()
        self.assertEqual(list(iterator), ['original'])

    def test_partial_iteration(self):
        mapping = grt.Dict()
        mapping['a'] = 'first'
        mapping['b'] = 'last'
        for _ in range(100):
            iterator = iter(mapping)
            self.assertEqual(next(iterator), 'first')
            del iterator
        gc.collect()


result = unittest.TextTestRunner(verbosity=2).run(
    unittest.defaultTestLoader.loadTestsFromTestCase(GrtDictIterationTests)
)
if not result.wasSuccessful():
    raise AssertionError('GRT dictionary iteration regression tests failed')
