iquery(3)
=====

# NAME

`iquery` - the Fuchsia Inspect API Query Toolkit

# SYNOPSIS

```
iquery [MODE] [--recursive] [--format=<FORMAT>]
       [(--full_paths|--absolute_paths)]
       PATH [...PATH]
```

# DESCRIPTION

`iquery` is a utility program for inspecting component objects exposed over the [Inspect API](inspect.md). It accepts a list of paths to process, and how they are processed depends on the `MODE` setting and options.

# MODES

`MODE=(--cat|--ls|--find)`

## `--cat`
> DEFAULT. Treat each `PATH` as an object directory to open, and print
> the properties and metrics for those objects.

## `--ls`
> Treat each `PATH` as an object directory, and list the children for those objects.

## `--find`
> Recursively find all object directories under the filesystem paths
> passed in, and output the relative path one per line.

# OPTIONS

## `--recursive`
> Continue to step down the hierarchy of elements. Mode dependent.
```
cat: If false, will print the top level object only. True will output the complete object hierarchy.
Example:
$ find .
a/
a/.channel
a/b/
a/b/.channel

$ iquery --cat a
a:
  key = value
  key2 = 3.4

$ iquery --cat --recursive a
a:
  a_key = a_value
  a_key2 = 3.4
  b:
    b_key = b_value
    b_key2 = 44.2

find: If false, it will descent each branch until it finds a valid object.
      True will output the complete tree, including nested objects.

Example:
$ find .
a/
a/.channel
a/b/
a/b/.channel

$ iquery --find .
a/

$ iquery --find --recursive .
a/
a/b/

ls: Currently ignored.
```

## `--format=<FORMAT>`
> What format the output should be in.
```
Current supported formatters:
- text: Meant for human inspection. This is the default option.
- json: Meant for machine consumption.
```

## `--full_paths`
> Rename each object to have its own relative path.
```
$ iquery a a/b
a:
b:
$ iquery --full_paths a a/b
a:
a/b:
```

## `--absolute_paths`
> Rename each object to have its own absolute path from the root.
```
$ cd /hub/c/
$ iquery a a/b
a:
b:
$ iquery --absolute_paths a a/b
/hub/c/a:
/hub/c/a/b:
```
