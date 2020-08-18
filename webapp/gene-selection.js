import 'bootstrap/js/dist/modal';
import $ from 'jquery';

export let geneSelectionEditor;

class GeneSelectionEditor {
	constructor() {
		this.dlog = document.getElementById("gene-selection-dialog");
	}

	show() {
		$(this.dlog).modal();
	}

	getOptions() {
		const f = document.geneSelectionForm;
		const fd = new FormData(f);

		const geneStartOffset = parseInt(document.getElementById('geneStartOffset').value);

		let geneStart = document.getElementById("geneStartType").value;
		if (geneStartOffset > 0)
			geneStart += "+" + geneStartOffset;
		else if (geneStartOffset < 0)
			geneStart += geneStartOffset;

		fd.append("gene-start", geneStart);

		const geneEndOffset = parseInt(document.getElementById('geneEndOffset').value);

		let geneEnd = document.getElementById("geneEndType").value;
		if (geneEndOffset > 0)
			geneEnd += "+" + geneEndOffset;
		else if (geneEndOffset < 0)
			geneEnd += geneEndOffset;

		fd.append("gene-end", geneEnd);

		return fd;
	}

}

window.addEventListener('load', () => {
	geneSelectionEditor = new GeneSelectionEditor();

	const btn = document.getElementById('show-gene-selection-btn');
	if (btn)
		btn.addEventListener('click', () => {
			geneSelectionEditor.show();
		});
});