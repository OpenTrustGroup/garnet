package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"os"
	"time"

	"github.com/google/subcommands"
)

type cmdRecord struct {
	flags *flag.FlagSet

	targetHostname string
	filePrefix     string
	reportType     string
	stdout         bool
	captureConfig  *captureTraceConfig
}

type reportGenerator struct {
	generatorPath    string
	outputFileSuffix string
}

var (
	builtinReports = map[string]reportGenerator{
		"html": {getHtmlGenerator(), "html"},
	}
)

func NewCmdRecord() *cmdRecord {
	cmd := &cmdRecord{
		flags: flag.NewFlagSet("record", flag.ExitOnError),
	}
	cmd.flags.StringVar(&cmd.filePrefix, "file-prefix", "",
		"Prefix for trace file names.  Defaults to 'trace-<timestamp>'.")
	cmd.flags.StringVar(&cmd.targetHostname, "target", "", "Target hostname.")
	cmd.flags.StringVar(&cmd.reportType, "report-type", "html", "Report type.")
	cmd.flags.BoolVar(&cmd.stdout, "stdout", false,
		"Send the report to stdout, in addition to writing to file.")

	cmd.captureConfig = newCaptureTraceConfig(cmd.flags)
	return cmd
}

func (*cmdRecord) Name() string {
	return "record"
}

func (*cmdRecord) Synopsis() string {
	return "Record a trace on a target, download, and convert to HTML."
}

func (cmd *cmdRecord) Usage() string {
	usage := "traceutil record <options>\n"
	cmd.flags.Visit(func(flag *flag.Flag) {
		usage += flag.Usage
	})

	return usage
}

func (cmd *cmdRecord) SetFlags(f *flag.FlagSet) {
	*f = *cmd.flags
}

func (cmd *cmdRecord) Execute(_ context.Context, f *flag.FlagSet,
	_ ...interface{}) subcommands.ExitStatus {

	// Flag errors in report type early.
	reportGenerator := builtinReports[cmd.reportType]
	generatorPath := ""
	outputFileSuffix := ""
	if reportGenerator.generatorPath != "" {
		generatorPath = reportGenerator.generatorPath
		outputFileSuffix = reportGenerator.outputFileSuffix
	} else {
		generatorPath = getExternalReportGenerator(cmd.reportType)
		outputFileSuffix = cmd.reportType
	}
	fmt.Printf("generator path: %s\n", generatorPath);
	if _, err := os.Stat(generatorPath); os.IsNotExist(err) {
		fmt.Printf("No generator for report type \"%s\"\n",
			cmd.reportType)
		return subcommands.ExitFailure
	}

	conn, err := NewTargetConnection(cmd.targetHostname)
	if err != nil {
		fmt.Println(err.Error())
		return subcommands.ExitFailure
	}
	defer conn.Close()

	err = captureTrace(cmd.captureConfig, conn)
	if err != nil {
		fmt.Println(err.Error())
		return subcommands.ExitFailure
	}

	prefix := cmd.getFilenamePrefix()
	jsonFilename := prefix + ".json"
	outputFilename := prefix + "." + outputFileSuffix

	if len(cmd.captureConfig.BenchmarkResultsFile) > 0 {
		fmt.Printf(
			"Downloading %v... ",
			cmd.captureConfig.BenchmarkResultsFile)
		err = conn.GetFile(
			cmd.captureConfig.BenchmarkResultsFile,
			cmd.captureConfig.BenchmarkResultsFile)
		if err != nil {
			fmt.Println(err)
			return subcommands.ExitFailure
		}
		fmt.Println("done")
	}

	fmt.Print("Downloading trace... ")
	// Should we use prefix on the remote file name as well?
	err = conn.GetFile("/data/trace.json", jsonFilename)
	if err != nil {
		fmt.Println(err.Error())
		return subcommands.ExitFailure
	}
	fmt.Println("done")

	// TODO(TO-403): Remove remote file.  Add command line option to leave it.

	title := cmd.getReportTitle()

	err = convertTrace(generatorPath, jsonFilename, outputFilename, title)
	if err != nil {
		fmt.Println(err.Error())
		return subcommands.ExitFailure
	}

	if cmd.stdout {
		report, openErr := os.Open(outputFilename)
		if openErr != nil {
			fmt.Println(openErr.Error())
			return subcommands.ExitFailure
		}
		_, reportErr := io.Copy(os.Stdout, report)
		if reportErr != nil {
			fmt.Println(reportErr.Error())
			return subcommands.ExitFailure
		}
	}

	return subcommands.ExitSuccess
}

func (cmd *cmdRecord) getFilenamePrefix() string {
	if cmd.filePrefix == "" {
		// Use ISO_8601 date time format.
		return "trace-" + time.Now().Format("2006-01-02T15:04:05")
	} else {
		return cmd.filePrefix
	}
}

func (cmd *cmdRecord) getReportTitle() string {
	conf := cmd.captureConfig
	have_categories := conf.Categories != ""
	have_duration := conf.Duration != 0
	text := ""
	if have_categories {
		text = text + ", categories " + conf.Categories
	}
	if have_duration {
		text = text + ", duration " + conf.Duration.String()
	}
	text = text[2:]
	if text == "" {
		return ""
	}
	return fmt.Sprintf("Report for %s", text)
}
