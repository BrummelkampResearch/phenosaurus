Phenosaurus
===========

As the intro page says: _An interactive platform for visualization and analysis of haploid screens_

This tool can be used for both _synthetic lethal_ as well as _intracellular phenotype_ screens.

The algorithms used for both are described below.

Building the software
---------------------

This software was developed using Linux and probably will not be able to run on a non-unix like operating system.

Other requirements are a very recent C++ compiler capable of compiling C++17 code. You will also need [cmake](https://cmake.org/) to build the software.

The required tools and libraries before building are:

* [boost](https://boost.org), the Boost libraries offer various C++ modules used in phenosaurus. These usually come with the OS.
* [Libzeep](https://github.com/mhekkel/libzeep.git) which is a library to build web applications
* [libpqxx](https://github.com/jtv/libpqxx.git), a library offering a modern C++ interface to PostgreSQL
* [mrc](https://github.com/mhekkel/mrc.git), not strictly required by very useful to bind resources with the application.
* [bowtie](http://bowtie-bio.sourceforge.net/manual.shtml), the program used to do the mapping of sequences on the genome.

Everything else needed should be built automatically.

Now it is a matter of entering the following commands:

```
mkdir build
cd build
cmake ..
cmake --build .
cmake --install .
```

Using the software
------------------

Before running the software you will have to create a configuration file. That file should be called `screen-analyzer.conf` and located in either the `.config` directory in your HOME or in the current directory where you're running the software.

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

Make sure the directories mentioned here exist and that the bowtie indices are downloaded and can be used.

### Running

The first argument to this application is a command to execute. There are currently four commands: 'create', 'map', 'analyze' and 'server'. Each has its own set of options.

The command 'create' creates a directory in the 'screen-dir' directory. It will also make a soft-link to the low and high FastQ files here.

The next command 'map' runs bowtie and parses the output into a compressed binary format and stores it inside the screen's directory. You can have multiple assemblies and read-lengths.

And once you've mapped the reads onto the genome you can run the command 'analyze'. The output this time is a tab delimited file containing:

* gene-name
* low-count
* high-count
* p-value
* fdr-corrected-p-value
* log2(mutation-index)

Each command has a help, e.g. `./screen-analyzer map --help`

This is a very condensed manual. I need to provide a better one, but maybe this will get you started.

Algorithms
----------

Here's a description on what is done in the analysis.

For all analysis, the first step is mapping the reads, provided in a FastQ file, onto the genome using bowtie version 1. The genome used is configured by the `assembly` option in the tools. At the NKI we have currently both _hg38_ and _hg19_ as option. For these assemblies, the bowtie indices are required.

The mapping is done in two steps, first with a setting to allow only unique hits without any mismatch and a read-length of 50 (which is a configurable option). After this, the reads that did not map, are tried again with a single mismatch allowed, again only unique hits are allowed. Both these results are merged and then compressed and stored on disk together with some metadata (versions used e.g.).

Assigning the insertions to a gene is done by using a reference mapping of gene transcriptions for the selected genome. The files providing this information we use are obtained from the [UCSC Genome Browser](https://genome.ucsc.edu/cgi-bin/hgTables), specifically we use the _NCBI RefSeq_ tracks.

The counts are done in the specified area around the gene's transcription and/or CDS regions with optional offsets in sense or antisense direction as specified.

Many calculations use the so-called _sense ratio_, the definition of this is:

```
sense_ratio = (counts_in_sense + 1) / (counts_in_sense + counts_in_antisense + 2);
```

The interpretation of these counts then depends on the analysis being done.

### Sythetic Lethal

The first step in sythetic lethal analysis is normalisation of the insertion counts. For all transcripts, the ones with at least 20 insertions in sense and antisense direction are selected in both the requested experiment as well as a single _sense ratio_ for the four control experiments. These transcripts are then sorted on _sense ratio_ of the control experiments and then divided into groups with an average group size of 500. For each group, the median _sense ratio_ is taken for both the experiment and the control experiments. The factor of these two values is used to correct the counts in the experiment.



1st step: binomial FDR corrected 2-sided binomial test on each individual dataset (experimental and control). Identify genes that make the bin_FDR cutoff (based on the least significant p-value found in the replicates). This is different from Blomen at al 2015 where a 1-sided test was used.

2nd step: for all genes a bi-directional fisher exact test is done to compare every gene with the control and do this for all combinations. p-value cutoff can be adjusted. Score the significance of the change and the direction of the change. If the directions are not identical in all tests the gene will not be considered a hit.

3 rd step: aggregate the experiment and control. Apply a greater fisher test and use the ‘odds ratio’ cutoff (maximum likelihood estimate). Standard setting: smaller than 0.8 (could be a SL) AND larger than 1/0.8 (could be a suppressor)

4rd step: 3 values are in browser (step 1, 2 and 3), set cutoffs, select hits. Place hits in table. Type of interaction is assigned based on: _sense ratio_ (based on aggregate) and binomial FDR (based on the least significant p-value among the replicates). Red is SL (intensity relates to odds ratio), Blue is suppressed essential. Purple is genotype-specific fitness enhancer. 
