# Copyright 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Runs spaceport.io's PerfMarks benchmark."""

import logging
import os

from telemetry import benchmark
from telemetry.core import util
from telemetry.page import page_measurement
from telemetry.page import page_set
from telemetry.value import list_of_scalar_values
from telemetry.value import scalar


class _SpaceportMeasurement(page_measurement.PageMeasurement):
  def __init__(self):
    super(_SpaceportMeasurement, self).__init__()

  def CustomizeBrowserOptions(self, options):
    options.AppendExtraBrowserArgs('--disable-gpu-vsync')

  def MeasurePage(self, page, tab, results):
    tab.WaitForJavaScriptExpression(
        '!document.getElementById("start-performance-tests").disabled', 60)

    tab.ExecuteJavaScript("""
        window.__results = {};
        window.console.log = function(str) {
            if (!str) return;
            var key_val = str.split(': ');
            if (!key_val.length == 2) return;
            __results[key_val[0]] = key_val[1];
        };
        document.getElementById('start-performance-tests').click();
    """)

    num_results = 0
    num_tests_in_spaceport = 24
    while num_results < num_tests_in_spaceport:
      tab.WaitForJavaScriptExpression(
          'Object.keys(window.__results).length > %d' % num_results, 180)
      num_results = tab.EvaluateJavaScript(
          'Object.keys(window.__results).length')
      logging.info('Completed test %d of %d' %
                   (num_results, num_tests_in_spaceport))

    result_dict = eval(tab.EvaluateJavaScript(
        'JSON.stringify(window.__results)'))
    for key in result_dict:
      chart, trace = key.split('.', 1)
      results.AddValue(scalar.ScalarValue(
          results.current_page, '%s.%s'% (chart, trace),
          'objects (bigger is better)', float(result_dict[key]),
          important=False))
    results.AddValue(list_of_scalar_values.ListOfScalarValues(
        results.current_page, 'Score', 'objects (bigger is better)',
                [float(x) for x in result_dict.values()]))


# crbug.com/166703: This test frequently times out on Windows.
@benchmark.Disabled('mac', 'win')
class Spaceport(benchmark.Benchmark):
  """spaceport.io's PerfMarks benchmark."""
  test = _SpaceportMeasurement

  def CreatePageSet(self, options):
    spaceport_dir = os.path.join(util.GetChromiumSrcDir(), 'chrome', 'test',
        'data', 'third_party', 'spaceport')
    ps = page_set.PageSet(file_path=spaceport_dir)
    ps.AddPageWithDefaultRunNavigate('file://index.html')
    return ps
