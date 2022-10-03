/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * 
 * Copyright (c) 2022 NKI/AVL, Netherlands Cancer Institute
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

const contextMenuActive = "context-menu--active";

let current;

export default class ContextMenu {

	constructor(menuID) {
		this.menu = document.getElementById(menuID);
		this.menuState = 0;
		this.target = null;

		document.addEventListener("contextmenu", (e) => this.handleContext(e));
		document.addEventListener("click", (e) => this.handleClick(e));
		window.addEventListener("keyup", (e) => {
			if (e.keyCode === 27) {
				this.toggleMenuOff();
			}
		});
		window.addEventListener("resize", () => this.toggleMenuOff());
	}

	static get current() {
		return current;
	}

	clickIsInsideTarget(e) {
		return false;
	}

	clickIsInsideItem(e) {
		let el = e.srcElement || e.target;

		do {
			if (el.tagName === "A" && el.classList && el.classList.contains("context-menu__link"))
				return el;

			el = el.parentNode;
		}
		while (el != null && el.tagName !== "UL");

		return false;
	}

	toggleMenuOn() {
		if (this.menuState !== 1) {
			this.menuState = 1;
			current = this;
			this.menu.classList.add(contextMenuActive);
		}
	}

	toggleMenuOff() {
		if (this.menuState !== 0) {
			this.menuState = 0;
			current = null;
			this.menu.classList.remove(contextMenuActive);
		}
	}

	static getPosition(e) {
		let posx = 0;
		let posy = 0;

		e = e || window.event;

		if (e.pageX || e.pageY) {
			posx = e.pageX;
			posy = e.pageY;
		} else if (e.clientX || e.clientY) {
			posx = e.clientX + document.body.scrollLeft + document.documentElement.scrollLeft;
			posy = e.clientY + document.body.scrollTop + document.documentElement.scrollTop;
		}

		return {
			x: posx,
			y: posy
		}
	}

	positionMenu(e) {
		const clickCoords = ContextMenu.getPosition(e);
		const clickCoordsX = clickCoords.x;
		const clickCoordsY = clickCoords.y;

		const menuWidth = this.menu.offsetWidth + 4;
		const menuHeight = this.menu.offsetHeight + 4;

		const windowWidth = window.innerWidth;
		const windowHeight = window.innerHeight;

		if ((windowWidth - clickCoordsX) < menuWidth) {
			this.menu.style.left = windowWidth - menuWidth + "px";
		} else {
			this.menu.style.left = clickCoordsX + "px";
		}

		if ((windowHeight - clickCoordsY) < menuHeight) {
			this.menu.style.top = windowHeight - menuHeight + "px";
		} else {
			this.menu.style.top = clickCoordsY + "px";
		}
	}

	handleContext(e) {
		this.target = this.clickIsInsideTarget(e);

		if (this.target) {
			e.preventDefault();
			this.toggleMenuOn();
			this.positionMenu(e);
		} else {
			this.target = null;
			this.toggleMenuOff();
		}
	}

	handleClick(e) {
		if (current !== this)
			return;

		let item = this.clickIsInsideItem(e);

		if (item) {
			e.preventDefault();
			this.toggleMenuOff();

			this.handleSelect(this.target, item.dataset.action);
		} else {
			var button = e.which || e.button;
			if (button === 1) {
				this.toggleMenuOff();
			}
		}
	}

	handleSelect(target, action) {
		alert(`unhandled action ${action} for target ${target.id}`);
	}
}
