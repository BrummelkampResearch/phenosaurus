import { highlightedGenes } from "./screenPlot";

// The container for the data in a dot on the screen.
// Dots can contain more than one value since more than one gene
// can score exactly the same.

export default class Dot {
    constructor(key, values) {

        this.key = key;
        this.values = values;

        this.mi = this.values[0].mi;
        this.log2mi = this.values[0].log2mi;
        this.insertions = this.values[0].insertions;

        this.subdot = false;
        this.multidot = this.values.length > 1;

        this.x = this.insertions;
        this.y = this.log2mi;
    }

    highlight() {
        return this.values.findIndex(g => highlightedGenes.has(g.geneName)) >= 0;
    }

    label() {
        return this.values.map(g => g.geneName).filter(g => highlightedGenes.has(g)).join(", ");
    }

    significant(pvCutOff) {
        return this.values.findIndex(d => d.fcpv < pvCutOff) >= 0;
    }
}