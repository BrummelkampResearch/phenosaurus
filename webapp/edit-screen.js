import "core-js/stable";
import "regenerator-runtime/runtime";

export class ScreenEditor {

	constructor() {
		this.form = document.forms['edit-screen-form'];
		this.csrf = this.form.elements['_csrf'].value;
		this.form.addEventListener('submit', (e) => this.submitForm(e));
		
		const btns = document.querySelectorAll("#assembly-table button");
		btns.forEach(btn => {
			btn.addEventListener("click", () => this.mapScreen(btn.dataset.assembly, btn));
		});
	}

	submitForm(e) {
		if (e) e.preventDefault();

		const screen = {
			name: this.form['screen-name'].value,
			published_name: this.form['screen-published-name'].value,
			// scientist: this.form['scientist'].value,
			// type: this.form['screen-type'].value,
			// detected_signal: this.form['detected-signal'].value,
			// genotype: this.form['genotype'].value,
			// treatment: this.form['treatment'].value,
			treatment_details: this.form['treatment-details'].value,
			cell_line: this.form['cell-line-clone'].value,
			description: this.form['description'].value,
			ignore: this.form['ignore'].checked,
			groups: []
			// files: []
		};

		[...document.querySelectorAll('#selected-groups input')]
			.filter(g => g.checked)
			.map(g => g.name)
			.forEach(g => screen.groups.push(g));

		let wasOK;
		fetch(`screen/${this.form['screen-name'].value}`, {
			body: JSON.stringify(screen),
			credentials: "include",
			method: 'PUT',
			headers: {
				'X-CSRF-Token': this.csrf,
				'Content-Type': 'application/json'
			}
		}).then(r => {
			wasOK = r.ok;
			return r.json();
		}).then(r => {
			if (r.error)
				throw r.error;
			if (wasOK == false)
				throw 'server returned an error';
			
			window.location = 'screens';
		}).catch(err => {
			console.log(err);
			alert(`Failed to submit form: ${err}`);
		});
	}

	mapScreen(assembly, btn) {
		fetch(`screen/${this.form['screen-name'].value}/map/${assembly}`, {
			credentials: "include",
			method: 'GET',
			headers: {
				'X-CSRF-Token': this.csrf
			}
		}).then(r => {
			if (r.ok) {
				btn.style.display = 'none';
				btn.parentNode.textContent = 'mapping started';
			}
			else alert("Failed to start mapping");
		}).catch(err => {
			console.log(err);
			alert(`Failed to map: ${err}`);
		});

	}
}

window.addEventListener("load", () => {
	new ScreenEditor();
});
