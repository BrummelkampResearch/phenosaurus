// make tables sortable

function sortColumn(th) {
	const tr = th.parentNode;
	const thead = tr.parentNode;
	const table = thead.parentNode;
	const tbody = table.getElementsByTagName("tbody")[0];
	const ths = Array.from(tr.getElementsByTagName("th"));
	const ix = ths.indexOf(th);

	const kt = tr.dataset.keytype.split('|');

	ths.forEach(th => th.classList.remove("sorted-asc", "sorted-desc"));

	const rowArray = [...tbody.getElementsByTagName("tr")];

	let desc = !table.sortDescending;
	if (table.sortedOnColumn !== ix) {
		desc = false;
		table.sortedOnColumn = ix;
	}
	table.sortDescending = desc;

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

	rowArray.forEach(row => tbody.appendChild(row));

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

