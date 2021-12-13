import * as d3 from "d3";
import ContextMenu from './context-menu';

// --------------------------------------------------------------------

export default class DotContextMenu extends ContextMenu {

	constructor(plot, menuID) {
		super(menuID ? menuID : "plot-context-menu");

		this.plot = plot;
		this.svg = plot.svg.node();
        this.uniqueScaleIndex = 0;
	}

	clickIsInsideTarget(e) {
		let el = e.srcElement || e.target;
		let values = null;

		while (el != null && el !== this.svg) {
			if (el.tagName === "g" && el.classList && el.classList.contains("dot")) {
				const data = d3.select(el).data();
				values = data[0].values;
				break;
			}

			el = el.parentNode;
		}

		if (el != null) {
			let action = this.menu.getElementsByClassName("gene-finder-action");
			if (action && action.length === 1)
				action[0].classList.toggle("disabled", values == null);

			action = this.menu.getElementsByClassName("gene-cards-action");
			if (action && action.length === 1)
				action[0].classList.toggle("disabled", values == null || values.length !== 1);
		}

		return el;
	}

	handleSelect(target, action) {
		const data = d3.select(target).data();
		const v = data[0] ? data[0].values : null;
		const genes = v ? v.map(g => g.gene).join(';') : null;

		switch (action) {
			case 'gene-finder':
				window.open("finder?gene=" + genes, "_blank");
				break;

			case 'gene-cards':
				window.open("https://www.genecards.org/cgi-bin/carddisp.pl?gene=" + genes, "_blank");
				break;

			case 'show-labels':
				this.plot.toggleLabels();
				break;

			case 'show-gridlines':
				this.plot.toggleGridLines();
				break;

				case 'presentation-mode':
					this.plot.togglePresentationMode();
					break;
	
				case 'unique-colours':
				{
                    this.uniqueScaleIndex = (this.uniqueScaleIndex + 1) % 3;
                    let uniqueScale;
                    
					switch (this.uniqueScaleIndex)
					{
						case 0:
							uniqueScale = d3.scaleSequential(d3.interpolatePiYG).domain([0, 9]);
							break;
							
						case 1:
							uniqueScale = d3.scaleSequential(d3.interpolateViridis).domain([0, 9]);
							break;
	
						case 2:
							uniqueScale = d3.scaleSequential(d3.interpolateCool).domain([0, 9]);
							break;
					}

                    this.plot.recolorGenes(uniqueScale);
					break;
				}
	
			case 'export-svg':
				this.plot.exportSVG();
				break;

			case 'export-data':
				this.plot.exportCSV();
				break;

			default:
				super.handleSelect(target, action);
		}
	}
}
