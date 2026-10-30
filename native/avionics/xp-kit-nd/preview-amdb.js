// SPDX-License-Identifier: GPL-3.0-or-later
// Browser-review data boundary only. Never contacts Navigraph or the simulator.
export class NavigraphAmdbClient {
  async searchForAirports(query) {
    const response = await fetch('/airports?q=' + encodeURIComponent(query));
    if (!response.ok) throw new Error('Local airport catalogue unavailable');
    return response.json();
  }
  async getAirportData(icao, include, exclude, projection = 'NAVIGRAPH:ARP_AZEQ') {
    const response = await fetch('/airport/' + encodeURIComponent(icao) + '?projection=' + encodeURIComponent(projection));
    if (!response.ok) throw new Error('Local airport geometry unavailable');
    const data = await response.json();
    return Object.fromEntries(Object.entries(data).filter(([name]) => (!include || include.includes(name)) && (!exclude || !exclude.includes(name))));
  }
}
