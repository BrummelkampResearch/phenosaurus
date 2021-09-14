import { highlightedGenes } from "./screenPlot";
import { significantGenes } from "./sl-screen";
import Dot from './dot';

export default class SLDot extends Dot {

    constructor(key, values) {
        super(key, values);

        this.sense_ratio = values[0].sense_ratio;

        this.y = this.sense_ratio;
    }

    highlight() {
        return this.values.findIndex(g => highlightedGenes.has(g.gene)) >= 0;
    }

    significant(pvCutOff) {
        return this.values.findIndex(d => d.binom_fdr < pvCutOff) >= 0;
    }
}
