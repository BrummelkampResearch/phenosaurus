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

import { highlightedGenes } from "./screenPlot";

// The container for the data in a dot on the screen.
// Dots can contain more than one value since more than one gene
// can score exactly the same.

export default class Dot {
    constructor(key, values) {

        this.key = key;
        this.values = values;

        this.mi = this.values[0].mi;
        this.log2mi = this.values[0].log2mi;
        this.insertions = this.values[0].insertions;

        this.subdot = false;
        this.multidot = this.values.length > 1;

        this.x = this.insertions;
        this.y = this.log2mi;
    }

    highlight() {
        return this.values.findIndex(g => highlightedGenes.has(g.gene)) >= 0;
    }

    label() {
        return this.values.map(g => g.gene).filter(g => highlightedGenes.has(g)).join(", ");
    }

    significant(pvCutOff) {
        return this.values.findIndex(d => d.fcpv < pvCutOff) >= 0;
    }
}