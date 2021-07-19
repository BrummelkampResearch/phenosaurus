// make tables sortable

function sortColumn(th) {
	const tr = th.parentNode;
	const ths = Array.from(tr.getElementsByTagName("th"));
	const ix = ths.indexOf(th);

	const kt = tr.dataset.keytype.split('|');

	ths.forEach(th => th.classList.remove("sorted-asc", "sorted-desc"));

	const rowArray = this.rows.map(r => r.tr);

	let desc = !this.sortDescending;
	if (this.sortedOnColumn !== ix) {
		desc = false;
		this.sortedOnColumn = ix;
	}
	this.sortDescending = desc;

	rowArray.sort((a, b) => {
		let ka = Array.from(a.getElementsByTagName("td"))[ix].innerText;
		let kb = Array.from(b.getElementsByTagName("td"))[ix].innerText;

		let d = 0;

		if (ka != kb) {
			switch (kt[ix]) {
				case 'b':
					break;

				case 's':
					ka = ka.toLowerCase();
					kb = kb.toLowerCase();
					d = ka.localeCompare(kb);
					break;

				case 'i':
				case 'f':
					ka = parseFloat(ka);
					kb = parseFloat(kb);

					if (Number.isNaN(ka) || Number.isNaN(kb))
						d = Number.isNaN(ka) ? -1 : 1;
					else
						d = ka - kb;
					break;
			}
		}

		return desc ? -d : d;
	});

	rowArray.forEach(row => this.tbody.appendChild(row));

	th.classList.add(desc ? "sorted-desc" : "sorted-asc");
}

window.addEventListener('load', () => {
	/* Code to make sortable tables */

	const tables = [...document.querySelectorAll('table.sortable-table')];
	tables.forEach(table => {
		const headings = table.querySelectorAll('th.sortable');
		headings.forEach(h => {
			h.addEventListener('click', (e) => {
				e.stopPropagation();
				sortColumn(e.target);
			});
		});
	});
});

