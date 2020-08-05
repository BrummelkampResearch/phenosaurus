import { highlightedGenes } from "./screenPlot";
import Dot from './dot';

export default class SLDot extends Dot {

    constructor(key, values) {
        super(key, values);

        this.senseratio = values[0].senseratio;

        this.y = this.senseratio;
    }

    highlight() {
        return this.values.findIndex(g => highlightedGenes.has(g.geneName)) >= 0;
    }

    significant(pvCutOff) {
        return this.values.findIndex(d => d.binom_fdr < pvCutOff) >= 0;
    }

    significantGene() {
        return this.values.findIndex(d => d.significant) >= 0;
    }
}