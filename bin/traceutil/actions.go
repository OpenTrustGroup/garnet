package main

import (
	"flag"
	"fmt"
	"strconv"
	"strings"
	"time"
)

const defaultCategories = "app,benchmark,gfx,input,kernel:meta,kernel:sched,ledger,magma,modular,motown,view,flutter,dart"

type captureTraceConfig struct {
	Categories           string
	BufferSize           uint // [MB]
	Duration             time.Duration
	BenchmarkResultsFile string
	SpecFile             string
}

func newCaptureTraceConfig(f *flag.FlagSet) *captureTraceConfig {
	config := &captureTraceConfig{}

	// TODO(TO-400): It would be nice to be able to specify +category or
	// -category to add or subtract from the default set.
	f.StringVar(&config.Categories, "categories", defaultCategories,
		"Comma separated list of categories to trace. \"all\" for all categories.")
	f.UintVar(&config.BufferSize, "buffer-size", 0,
		"Size of trace buffer in MB.")
	f.DurationVar(&config.Duration, "duration", 0,
		"Duration of trace capture (e.g. '10s').  Second resolution.")
	f.StringVar(&config.SpecFile, "spec-file", "",
		"Tracing specification file.")
	f.StringVar(
		&config.BenchmarkResultsFile,
		"benchmark-results-file",
		"",
		"Relative filepath for storing benchmark results.",
	)

	return config
}

func captureTrace(config *captureTraceConfig, conn *TargetConnection) error {
	cmd := []string{"trace", "record"}
	if config.Categories == "" {
		config.Categories = defaultCategories
	}

	if len(config.SpecFile) > 0 {
		cmd = append(cmd, "--spec-file="+config.SpecFile)
	}

	if len(config.BenchmarkResultsFile) > 0 {
		cmd = append(cmd, "--benchmark-results-file="+config.BenchmarkResultsFile)
	}

	if config.Categories != "all" {
		cmd = append(cmd, "--categories="+config.Categories)
	}

	if config.BufferSize != 0 {
		cmd = append(cmd, "--buffer-size="+
			strconv.FormatUint(uint64(config.BufferSize), 10))
	}
	if config.Duration != 0 {
		cmd = append(cmd, "--duration="+
			strconv.FormatUint(uint64(config.Duration.Seconds()), 10))
	}

	// TODO(TO-401): The target `trace` command's output is misleading.
	// Specifically it says "Trace file written to /tmp/trace.json" which
	// references where the file is written to on the target not on the host.
	// We should wrap this and offer less confusing status.
	return conn.RunCommand(strings.Join(cmd, " "))
}

func convertTrace(generator string, inputPath string, outputPath string,
		title string) error {
	fmt.Printf("Converting %s to %s... ", inputPath, outputPath)
	var args []string
	args = append(args, "--output=" + outputPath)
	if title != "" {
		args = append(args, "--title=" + title)
	}
	args = append(args, inputPath)
	err := runCommand(generator, args)
	if err != nil {
		fmt.Printf("failed: %s\n", err.Error())
		fmt.Printf("Invoked as: %s %s\n", generator, args);
	} else {
		fmt.Println("done.")
	}
	return err
}
