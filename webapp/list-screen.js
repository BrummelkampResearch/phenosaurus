import "core-js/stable";
import "regenerator-runtime/runtime";

window.addEventListener("load", () => {
	const createBtn = document.getElementById('add-screen-btn');
	if (createBtn)
		createBtn.addEventListener('click', () => {
			window.location = 'create-screen';
		});
	
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


});