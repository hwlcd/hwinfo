# hwinfo

Collect hardware info

Currently supported metrics and platforms

| Metric                | OSX (Arm64) | OSX (Intel) | Linux | Windows |
| --------------------- | ----------- | ----------- | ----- | ------- |
| Cpu Temperature       | [Yes]       | [ ]         | [ ]   | [ ]     |
| Gpu Temperature       | [Yes]       | [ ]         | [ ]   | [ ]     |
| Memory Temperature    | [Yes]       | [ ]         | [ ]   | [ ]     |
| Disk Temperature      | [Yes]       | [ ]         | [ ]   | [ ]     |
| Battery Temperature   | [Yes]       | [ ]         | [ ]   | [ ]     |
| Fan Speed             | [Yes]       | [ ]         | [ ]   | [ ]     |
| Cpu Power Consumption | [Yes]       | [ ]         | [ ]   | [ ]     |
| Gpu Power Consumption | [Yes]       | [ ]         | [ ]   | [ ]     |
| Cpu Usage             | [Yes]       | [ ]         | [ ]   | [ ]     |
| Cpu Frequency         | [Yes]       | [ ]         | [ ]   | [ ]     |
| Gpu Usage             | [No]        | [ ]         | [ ]   | [ ]     |
| Gpu Frequency         | [Yes]       | [ ]         | [ ]   | [ ]     |
| Memory Usage          | [Yes]       | [ ]         | [ ]   | [ ]     |
| Disk IO               | [Yes]       | [ ]         | [ ]   | [ ]     |
| Network IO            | [Yes]       | [ ]         | [ ]   | [ ]     |

Library dependencies:

- boost

Cli dependencies:

- ncurses

## How to build

Use cmake to build the project.

For example:

```bash
mkdir output
cmake ..
cmake --build .
```

## About Cli

The command line tool has three output types:

- Text mode: Use -t or --text, all metrics will be printed as plain text
- Json mode: Use -j or --json, all metrics will be printed as a json string in one line
- Terminal mode: Default mode which will print metrics via a simple terminal ui

The program will run forever with default options unless interrupted by ctrl + c for press q for terminal mode. Use -n x or --number x to specify how many reports you want.
