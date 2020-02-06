import '@fortawesome/fontawesome-free/css/all.min.css';
import 'bootstrap';
import 'bootstrap/js/dist/modal'
import 'bootstrap/dist/css/bootstrap.min.css';

class LoginDialog {
	constructor(callback) {
		this.dialog = null;

		fetch("loginDialog")
			.then(dlg => dlg.text())
			.then(dlg => {
				const div = document.createElement("div");
				div.innerHTML = dlg;
				document.body.appendChild(div);

				this.form = document.getElementById("local-login-form");
				this.form.addEventListener('submit', e => this.submit(e));

				this.dialog = document.getElementById('loginDialogID');
				this.error = document.getElementById("failedToLoginMsg");

				this.show(null, callback);
			})
			.catch(e => {
				console.log(e);
				alert('Error displaying login dialog');
			})
	}

	show(evt, callback) {
		if (evt != null) {
			evt.stopPropagation();
			evt.preventDefault();
		}

		this.callback = callback;

		$(this.error).hide();
		$(this.dialog).modal();
	}

	submit(evt) {
		if (evt != null) {
			evt.stopPropagation();
			evt.preventDefault();
		}

		const data = new FormData(this.form);
		fetch('ajax/login', {
			method: 'POST',
			body: data
		}).then(value => {
			if (value.ok)
				return value.json();
			throw "failed";
		}).then(value => {
			if (value.ok) {
				$(this.dialog).modal('hide');
				if (this.callback != null)
					this.callback();
			}
			else
				throw "failed";
		}).catch(err => {
			$(this.error).show();
		});
	}
}

let loginDialog = null;

export function showLoginDialog(e, callback) {
	if (loginDialog == null)
		loginDialog = new LoginDialog(callback);
	else
		loginDialog.show(e, callback);
}
