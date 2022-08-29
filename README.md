Phenosaurus
===========

As the intro page says: _An interactive platform for visualization and analysis of haploid screens_

This tool can be used for both _synthetic lethal_ as well as _intracellular phenotype_ screens.

The algorithms used for both are described below.

Building the software
---------------------

This software was developed using Linux and probably will not be able to run on a non-unix like operating system.

Other requirements



This is the documentation for the application called screen-analyzer. You can copy the current version in my home directory on stallman to a location where you want to run it. Or simply run the one from my home.

The first argument to this application is a command to execute. There are currently four commands: 'create', 'map', 'analyze' and 'server'. Each has its own set of options.

Some of the options can be set in a config file. The application will look for a file called 'screen-analyzer.conf' in the current directory (where you run the command) and if not found in your home directory.

The config file in my home has the following settings:

```
screen-dir=/srv/data/screens
transcripts-dir=/srv/data/transcripts
bowtie=/usr/bin/bowtie
threads=15
bowtie-index-hg19=/references/genomes/BOWTIE_HG19/hg19
bowtie-index-hg38=/references/genomes/BOWTIE_GRCh38/GCA_000001405.15_GRCh38_no_alt_analysis_set
assembly=hg38
trim-length=50
control=ControlData-HAP1
```

You can for now create a directory similar to mine on /media/data/nas_scratch. I propose to move this to a central location once the software is finished.

The command 'create' creates a directory in the 'screen-dir' directory. It will also make a soft-link to the low and high FastQ files here.

The next command 'map' runs bowtie and parses the output into a compressed binary format and stores it inside the screen's directory. You can have multiple assemblies and read-lengths.

And once you've mapped the reads onto the genome you can run the command 'analyze'. The output this time is a tab delimited file containing:

gene-name, low-count, high-count, p-value, fdr-corrected-p-value, log2(mutation-index)

Each command has a help, e.g. `./screen-analyzer map --help`

This is a very condensed manual. I need to provide a better one, but maybe this will get you started.
