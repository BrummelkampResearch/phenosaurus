import "core-js/stable";
import "regenerator-runtime/runtime";

class ScreenEditor {

	constructor() {
		this.form = document.forms['edit-screen-form'];
		this.csrf = this.form.elements['_csrf'].value;
		this.form.addEventListener('submit', (e) => this.submitForm(e));
	}

	submitForm(e) {
		if (e) e.preventDefault();

		const screen = {
			name: this.form['screen-name'].value,
			// scientist: this.form['scientist'].value,
			// type: this.form['screen-type'].value,
			// detected_signal: this.form['detected-signal'].value,
			// genotype: this.form['genotype'].value,
			// treatment: this.form['treatment'].value,
			treatment_details: this.form['treatment-details'].value,
			cell_line: this.form['cell-line-clone'].value,
			description: this.form['description'].value,
			ignore: this.form['ignore'].checked,
			// files: []
		};

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
			
			this.form.reset();
		}).catch(err => {
			console.log(err);
			alert(`Failed to submit form: ${err}`);
		});

	}
}

window.addEventListener("load", () => {
	new ScreenEditor();
});