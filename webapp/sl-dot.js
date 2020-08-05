import Dot from './dot';

export default class SLDot extends Dot {

    constructor(key, values) {
        super(key, values);

        this.senseratio = values[0].senseratio;
    }

    significant(pvCutOff) {
        return this.values.findIndex(d => d.binom_fdr < pvCutOff) >= 0;
    }

    significantGene(significantGenes) {
        return this.values.findIndex(d => significantGenes.has(d.geneName)) >= 0;
    }
}