// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package benchmarking

import (
	"math"
	"reflect"
	"testing"
)

func getTestTrace() []byte {
	testTrace := []byte(`
		{
		  "displayTimeUnit": "ns",
		  "traceEvents": [{
			  "cat": "async",
			  "name": "ReadWrite",
			  "ts": 687503138,
			  "id": 43,
			  "pid": 7009,
			  "tid": 7022,
			  "ph": "b"
			}, {
			  "cat": "input",
			  "name": "Read",
			  "ts": 697503138.9531089,
			  "pid": 7009,
			  "tid": 7021,
			  "ph": "B"
			}, {
			  "cat": "input",
			  "name": "Read",
			  "ts": 697503461.7395687,
			  "pid": 7009,
			  "tid": 7021,
			  "ph": "E"
			}, {
			  "cat": "io",
			  "name": "Write",
			  "ts": 697778328.2160872,
			  "pid": 7009,
			  "tid": 7022,
			  "ph": "B"
			}, {
			  "cat": "io",
			  "name": "Write",
			  "ts": 697778596.5994568,
			  "pid": 7009,
			  "tid": 7022,
			  "ph": "E"
			}, {
			  "cat": "io",
			  "name": "Read",
			  "ts": 697868185.3588456,
			  "pid": 7010,
			  "tid": 7023,
			  "ph": "B"
			}, {
			  "cat": "io",
			  "name": "Read",
			  "ts": 697868571.6018075,
			  "pid": 7010,
			  "tid": 7023,
			  "ph": "E"
			}, {
			  "cat": "async",
			  "name": "ReadWrite",
			  "ts": 687503138,
			  "id": 43,
			  "pid": 7009,
			  "tid": 7022,
			  "ph": "e"
			},
			{
			  "name": "log",
			  "ph": "i",
			  "ts": 7055567057.312,
			  "pid": 5945,
			  "tid": 5962,
			  "s": "g",
			  "args": {
				"message": "[INFO:trace_manager.cc(66)] Stopping trace"
			  }
			}
		  ],
		  "systemTraceEvents": {
			"type": "fuchsia",
			"events": [{
			  "ph": "p",
			  "pid": 7009,
			  "name": "root_presenter"
			}, {
			  "ph": "t",
			  "pid": 7009,
			  "tid": 7022,
			  "name": "initial-thread"
			}]
		  }
		}`)
	return testTrace
}

func TestReadTrace(t *testing.T) {
	expectedModel := Model{
		Processes: []Process{
			Process{Name: "root_presenter", Pid: 7009, Threads: []Thread{
				Thread{Name: "", Tid: 7021, Events: []Event{
					Event{Type: 0, Cat: "input", Name: "Read", Pid: 7009, Tid: 7021, Start: 6.975031389531089e+08, Dur: 322.78645980358124, Id: 0, Args: map[string]interface{}(nil)}}},
				Thread{Name: "initial-thread", Tid: 7022, Events: []Event{
					Event{Type: 0, Cat: "io", Name: "Write", Pid: 7009, Tid: 7022, Start: 6.977783282160872e+08, Dur: 268.38336956501007, Id: 0, Args: map[string]interface{}(nil)},
					Event{Type: 1, Cat: "async", Name: "ReadWrite", Pid: 7009, Tid: 7022, Start: 6.87503138e+08, Dur: 0, Id: 43, Args: map[string]interface{}(nil)}}}}},
			Process{Name: "", Pid: 7010, Threads: []Thread{
				Thread{Name: "", Tid: 7023, Events: []Event{
					Event{Type: 0, Cat: "io", Name: "Read", Pid: 7010, Tid: 7023, Start: 6.978681853588456e+08, Dur: 386.2429618835449, Id: 0, Args: map[string]interface{}(nil)}}}}},
			Process{Name: "", Pid: 5945, Threads: []Thread{
				Thread{Name: "", Tid: 5962, Events: []Event{
					Event{Type: 2, Cat: "", Name: "log", Pid: 5945, Tid: 5962, Start: 7.055567057312e+09, Dur: 0, Id: 0, Args: map[string]interface{}{"message": "[INFO:trace_manager.cc(66)] Stopping trace"}}}}}}}}
	model, err := ReadTrace(getTestTrace())

	if err != nil {
		t.Fatalf("Processing the trace produced an error: %#v\n", err)
	}

	if !reflect.DeepEqual(expectedModel, model) {
		t.Error("Generated model and expected model are different\n")
	}
}

func compareEvents(t *testing.T, description string, expectedEvents []Event, events []Event) {
	if !reflect.DeepEqual(expectedEvents, events) {
		if len(expectedEvents) != len(events) {
			t.Errorf("%s: Expecting %d events, got %d events\n", description, len(expectedEvents), len(events))
		} else {
			t.Errorf("%s: Expected and retrieved events did not match\n", description)
		}
	}
}

func TestFindEvents(t *testing.T) {
	model, _ := ReadTrace(getTestTrace())

	// Find events by Name
	name := "Read"
	expectedEvents := []Event{
		Event{Type: 0, Cat: "input", Name: "Read", Pid: 7009, Tid: 7021, Start: 6.975031389531089e+08, Dur: 322.78645980358124, Id: 0, Args: map[string]interface{}(nil)},
		Event{Type: 0, Cat: "io", Name: "Read", Pid: 7010, Tid: 7023, Start: 6.978681853588456e+08, Dur: 386.2429618835449, Id: 0, Args: map[string]interface{}(nil)}}
	events := model.FindEvents(EventsFilter{Name: &name})
	compareEvents(t, "Find events by Name", expectedEvents, events)

	// Find events by Category
	cat := "io"
	expectedEvents = []Event{
		Event{Type: 0, Cat: "io", Name: "Write", Pid: 7009, Tid: 7022, Start: 6.977783282160872e+08, Dur: 268.38336956501007, Id: 0, Args: map[string]interface{}(nil)},
		Event{Type: 0, Cat: "io", Name: "Read", Pid: 7010, Tid: 7023, Start: 6.978681853588456e+08, Dur: 386.2429618835449, Id: 0, Args: map[string]interface{}(nil)}}
	events = model.FindEvents(EventsFilter{Cat: &cat})
	compareEvents(t, "Find events by Category", expectedEvents, events)

	// Find events by Process
	pid := 7009
	expectedEvents = []Event{
		Event{Type: 0, Cat: "input", Name: "Read", Pid: 7009, Tid: 7021, Start: 6.975031389531089e+08, Dur: 322.78645980358124, Id: 0, Args: map[string]interface{}(nil)},
		Event{Type: 0, Cat: "io", Name: "Write", Pid: 7009, Tid: 7022, Start: 6.977783282160872e+08, Dur: 268.38336956501007, Id: 0, Args: map[string]interface{}(nil)},
		Event{Type: 1, Cat: "async", Name: "ReadWrite", Pid: 7009, Tid: 7022, Start: 6.87503138e+08, Dur: 0, Id: 43, Args: map[string]interface{}(nil)}}
	events = model.FindEvents(EventsFilter{Pid: &pid})
	compareEvents(t, "Find events by Process", expectedEvents, events)

	// Find events by Thread
	tid := 7022
	expectedEvents = []Event{
		Event{Type: 0, Cat: "io", Name: "Write", Pid: 7009, Tid: 7022, Start: 6.977783282160872e+08, Dur: 268.38336956501007, Id: 0, Args: map[string]interface{}(nil)},
		Event{Type: 1, Cat: "async", Name: "ReadWrite", Pid: 7009, Tid: 7022, Start: 6.87503138e+08, Dur: 0, Id: 43, Args: map[string]interface{}(nil)}}
	events = model.FindEvents(EventsFilter{Tid: &tid})
	compareEvents(t, "Find events by Thread", expectedEvents, events)

	// Find events by Name and Category
	expectedEvents = []Event{
		Event{Type: 0, Cat: "io", Name: "Read", Pid: 7010, Tid: 7023, Start: 6.978681853588456e+08, Dur: 386.2429618835449, Id: 0, Args: map[string]interface{}(nil)}}
	events = model.FindEvents(EventsFilter{Name: &name, Cat: &cat})
	compareEvents(t, "Find events by Name and Category", expectedEvents, events)
}

func compareAvgDurations(t *testing.T, listSize int, expected float64, actual float64) {
	if expected != actual {
		t.Errorf("Expected average duration of %d events is: %v, actual is: %v\n", listSize, expected, actual)
	}
}

func TestAvgDuration(t *testing.T) {
	// Average of Zero events
	eventList := make([]Event, 0)
	avg := AvgDuration(eventList)
	if !math.IsNaN(avg) {
		t.Errorf("Expected average duration of Zero events is: NaN, actual is: %v\n", avg)
	}

	// Average of One events.
	eventList = []Event{
		Event{Type: 0, Cat: "io", Name: "Write", Pid: 7009, Tid: 7022, Start: 6.977783282160872e+08, Dur: 268.38336956501007, Id: 0, Args: map[string]interface{}(nil)}}
	avg = AvgDuration(eventList)
	compareAvgDurations(t, len(eventList), eventList[0].Dur, avg)

	// Average of Two events.
	eventList = []Event{
		Event{Type: 0, Cat: "input", Name: "Read", Pid: 7009, Tid: 7021, Start: 6.975031389531089e+08, Dur: 322.78645980358124, Id: 0, Args: map[string]interface{}(nil)},
		Event{Type: 0, Cat: "io", Name: "Read", Pid: 7010, Tid: 7023, Start: 6.978681853588456e+08, Dur: 386.2429618835449, Id: 0, Args: map[string]interface{}(nil)}}
	avg = AvgDuration(eventList)
	compareAvgDurations(t, len(eventList), (eventList[0].Dur+eventList[1].Dur)/2.0, avg)
}
