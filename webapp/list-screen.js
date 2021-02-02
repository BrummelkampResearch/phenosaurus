import "core-js/stable";
import "regenerator-runtime/runtime";

function attachEventListeners() {
	[...document.getElementsByClassName('edit-screen-btn')]
		.forEach(btn => {
			btn.addEventListener('click', e => {
				if (e) e.preventDefault();

				const screenID = btn.dataset["id"];
				window.location = `edit-screen?screen-id=${screenID}`;
			})
		});	

		[...document.getElementsByClassName('delete-screen-btn')]
		.forEach(btn => {
			btn.addEventListener('click', e => {
				if (e) e.preventDefault();

				const screenID = btn.dataset["id"];
				if (confirm(`Are you sure you want to delete screen ${screenID}`)) {
					fetch(`screen/${screenID}`, {
						method: 'DELETE',
						credentials: 'include'
					}).then(r => {
						return r.json();
					}).then(r => {
						if (r && typeof r.error === 'string')
							throw r.error;
						window.location.reload();
					}).catch(err => {
						console.log(err);
						alert(err);
					})
				}
			})
		});
}

window.addEventListener("load", () => {
	const createBtn = document.getElementById('add-screen-btn');
	if (createBtn)
		createBtn.addEventListener('click', () => {
			window.location = 'create-screen';
		});
	
	attachEventListeners();

	// refresh the list every 30 seconds
	const table = document.getElementById('screen-table');
	const iv = setInterval(() => {
		fetch('screen-table', { credentials: 'include' })
		.then(r => {
			if (r.ok)
				return r.text();
			throw 'no data';
		}).then(t => {
			if (typeof t !== "string" || t.length == 0)
				throw 'empty string?';

			const container = document.createElement('div');
			container.innerHTML = t;
			
			const tbody = table.tBodies[0];
			[...tbody.querySelectorAll("tr")]
				.forEach(tr => tbody.removeChild(tr));

			[...container.querySelectorAll('tbody > tr')]
				.forEach(tr => {
					// tr.parentElement().removeChild(tr);
					tbody.appendChild(tr);
				});
			
			attachEventListeners();
		}).catch(err => console.log(err));
	}, 15000);

});