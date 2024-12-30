export function fillTable(table, data, splitter) {
	const oldTBody = table.tBodies[0];
	const newTBody = document.createElement("tbody");
	data.forEach(element => {
		const row = document.createElement("tr");
		const fields = splitter(element);
		fields.forEach(field => {
			const td = document.createElement("td");
			td.innerText = field;
			row.appendChild(td);
		});
		newTBody.appendChild(row);
	});
	table.replaceChild(newTBody, oldTBody);
}
