import * as d3 from 'd3';

const d3fmt1 = d3.format(".1g");
const d3fmt2 = d3.format(".2g");
const d3fmt3 = d3.format(".3g");

export function format_pv(pv, f = 2) {

	if (pv) {
		switch (f) {
			case 1: return d3fmt1(pv);
			case 2: return d3fmt2(pv);
			case 3: return d3fmt3(pv);
			default: return d3.format(`.${f}g`)(pv);
		}
	}

	return "<1.1e-38";
}
