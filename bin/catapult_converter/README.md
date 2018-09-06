
# Catapult performance results converter

This directory contains the `catapult_converter` command line tool
which takes perf test results in [our format] and converts them to the
[Catapult Dashboard](https://github.com/catapult-project/catapult)'s
[JSON "HistogramSet" format](
https://github.com/catapult-project/catapult/blob/master/docs/histogram-set-json-format.md).

## Parameters

The Catapult dashboard requires the following parameters (called
"diagnostics") to be present in each HistogramSet that is uploaded to
the dashboard:

* chromiumCommitPositions: This parameter is taken from the
  `--execution-timestamp-ms` argument.  The dashboard uses this value
  to order results from different builds in a graph.

* benchmarks: This parameter is taken from the `test_suite` field in
  the JSON input file.  This is often the name of the executable
  containing the perf tests, prefixed by "fuchsia.",
  e.g. "fuchsia.zircon_benchmarks".

* masters: The term "master" is an outdated term from when Buildbot
  was used by Chrome infrastructure.  The convention now is to use the
  name of the bucket containing the builder for this parameter.

* bots: The term "bot" is an outdated term from when Buildbot was used
  by Chrome infrastructure.  The convention now is to use the name of
  the builder for this parameter.

* logUrls: This parameter is taken from the `--log-url` argument.

  This should contain a link to the LUCI build log page (or a page on
  any similar continuous integration system) for the build that
  produced the perf test results.  The Catapult dashboard will show
  this link if you select a point on a performance results graph.

  Note: Although the converter requires the `--log-url` argument, the
  Catapult dashboard does not require the logUrls field in the
  HistogramSet data.

For more information on Catapult's format, see [How to Write
Metrics](https://github.com/catapult-project/catapult/blob/master/docs/how-to-write-metrics.md).

[our format]: https://fuchsia.googlesource.com/docs/+/master/development/benchmarking/results_schema.md
