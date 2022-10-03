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

Here's a description on what is done in an analysis.

For all analyses, the first step is mapping the reads, provided in a FastQ file, onto the genome using bowtie version 1. The genome used is configured by the `assembly` option in the tools. At the NKI we have currently both _hg38_ and _hg19_ as option. For these assemblies, the bowtie indices are required.

The mapping is done in two steps, first with a setting to allow only unique hits without any mismatch and a read-length of 50 (which is a configurable option). After this, the reads that did not map, are tried again with a single mismatch allowed, again only unique hits are allowed. Both these results are merged and then compressed and stored on disk together with some metadata (versions used e.g.).

Assigning the insertions to a gene is done by using a reference mapping of gene transcriptions for the selected genome. The files providing this information we use are obtained from the [UCSC Genome Browser](https://genome.ucsc.edu/cgi-bin/hgTables), specifically we use the _NCBI RefSeq_ tracks.

The counts are done in the specified area around the gene's transcription and/or CDS regions with optional offsets in sense or antisense direction as specified.

Many calculations use the so-called _sense ratio_, the definition of this is:

```
sense_ratio = (counts_in_sense + 1) / (counts_in_sense + counts_in_antisense + 2);
```

The interpretation of these counts then depends on the analysis being done.

### Synthetic Lethal

The first step in sythetic lethal analysis is normalisation of the insertion counts. For all transcripts, the ones with at least 20 insertions in sense and antisense direction are selected in both the requested experiment as well as a single, aggregated _sense ratio_ for the four control experiments. These transcripts are then sorted on _sense ratio_ of the control experiments and then divided into groups with an average group size of 500. For each group, the median _sense ratio_ is taken for both the experiment and the control experiments. The idea is that the median in both the controls and the experiment should be the same per group and so we adjust the counts to achieve this.

For each experiment a _two-sided binomial test_ is calculated which gives a _p-value_ for each gene. These _p-values_ are then corrected for _False Discovery Rate_ using the _Benjamini-Hochberg Procedure_. This value is stored as `binom_fdr` in the tables and the least significant _p-value_ found is used to determine if this gene is considered to be a hit based on a user specified cut-off value.

Next to this _p-value_ a _Fisher's exact test_ is calculated for the experiment versus all four control data sets.

Then these intermediate results are combined. First, for each replicate the direction of the score compared to the controls is calculated and if it is inconsistent, the gene is not considered to be a hit. Then a _right-tailed_  _Fisher's exact test_ test is performed on the aggregated counts of the replicates in the experiment and the ones in the control data set. A side product of calculating this  _Fisher's exact test_ is a so-called _Odds Ratio_, this is stored as well.

The _Odds Ratio_ is compared to a user-supplied cut-off value, which is 0.8 by default. Genes with a value smaller than 0.8 are considered to be _sythetic lethal_ whereas values larger than 1/0.8 could be suppressors.

In the web interface, the values are displayed in a Fishtail graph and the values that meet the cut-offs are displayed in tables. A colour is used to identify the various types, red is _synthetic lethal_, blue is _suppressed essential_ and purple is a _genotype-specific fitness enhancer_. The intensity of the colour is related to the _odds ratio_.
