import * as d3 from 'd3';

const d3fmt = d3.format(".2g");

export function format_pv(pv) {

	return pv ? d3fmt(pv) : "<1.1e-38";
}
