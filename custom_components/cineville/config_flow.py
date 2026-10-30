"""Set up a city, as on cineville.nl: choose the city, then (if you like) only some of its theaters."""

from __future__ import annotations

from typing import Any

import voluptuous as vol

from homeassistant.config_entries import ConfigEntry, ConfigFlow, ConfigFlowResult, OptionsFlow
from homeassistant.core import callback
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.selector import (
    NumberSelector,
    NumberSelectorConfig,
    NumberSelectorMode,
    SelectOptionDict,
    SelectSelector,
    SelectSelectorConfig,
    SelectSelectorMode,
)

from .api import CinevilleApi, CinevilleError
from .const import (
    CONF_CITY,
    CONF_DAYS,
    CONF_LANGUAGE,
    CONF_SOON_WEEKS,
    CONF_SORT,
    CONF_VENUES,
    DEFAULT_DAYS,
    DEFAULT_SOON_WEEKS,
    DEFAULT_SORT,
    DOMAIN,
    LANGUAGES,
    SORTS,
)
from .films import cities, venues_in


def _venue_selector(venues: list[dict]) -> SelectSelector:
    return SelectSelector(SelectSelectorConfig(
        options=[SelectOptionDict(value=v["id"], label=v.get("name") or v["id"]) for v in venues if v.get("id")],
        multiple=True,
        mode=SelectSelectorMode.LIST,
    ))


def _settings_schema(venues: list[dict], current: dict[str, Any]) -> vol.Schema:
    known = {v.get("id") for v in venues}
    chosen = [v for v in current.get(CONF_VENUES, []) if v in known]
    return vol.Schema({
        vol.Optional(CONF_VENUES, default=chosen): _venue_selector(venues),
        vol.Required(CONF_DAYS, default=current.get(CONF_DAYS, DEFAULT_DAYS)): NumberSelector(
            NumberSelectorConfig(min=1, max=14, step=1, mode=NumberSelectorMode.SLIDER)),
        vol.Required(CONF_SORT, default=current.get(CONF_SORT, DEFAULT_SORT)): SelectSelector(
            SelectSelectorConfig(options=SORTS, translation_key="sort", mode=SelectSelectorMode.DROPDOWN)),
        vol.Required(CONF_SOON_WEEKS, default=current.get(CONF_SOON_WEEKS, DEFAULT_SOON_WEEKS)): NumberSelector(
            NumberSelectorConfig(min=1, max=12, step=1, mode=NumberSelectorMode.SLIDER)),
        vol.Required(CONF_LANGUAGE, default=current.get(CONF_LANGUAGE, "auto")): SelectSelector(
            SelectSelectorConfig(options=LANGUAGES, translation_key="language", mode=SelectSelectorMode.DROPDOWN)),
    })


def _clean(user_input: dict[str, Any]) -> dict[str, Any]:
    out = dict(user_input)
    out[CONF_DAYS] = int(out.get(CONF_DAYS, DEFAULT_DAYS))
    out[CONF_SOON_WEEKS] = int(out.get(CONF_SOON_WEEKS, DEFAULT_SOON_WEEKS))
    out[CONF_VENUES] = sorted(out.get(CONF_VENUES) or [])
    return out


class CinevilleConfigFlow(ConfigFlow, domain=DOMAIN):
    VERSION = 1

    def __init__(self) -> None:
        self._venues: list[dict] = []
        self._city: str | None = None

    async def _load(self) -> bool:
        if self._venues:
            return True
        try:
            self._venues = await CinevilleApi(async_get_clientsession(self.hass)).venues()
        except CinevilleError:
            return False
        return bool(self._venues)

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if not await self._load():
            errors["base"] = "cannot_connect"
            return self.async_show_form(step_id="user", data_schema=vol.Schema({}), errors=errors)
        if user_input is not None:
            self._city = user_input[CONF_CITY]
            return await self.async_step_theaters()
        options = cities(self._venues)
        default = "Amsterdam" if "Amsterdam" in options else options[0]
        schema = vol.Schema({
            vol.Required(CONF_CITY, default=default): SelectSelector(
                SelectSelectorConfig(options=options, mode=SelectSelectorMode.DROPDOWN, sort=False)),
        })
        return self.async_show_form(step_id="user", data_schema=schema, errors=errors)

    async def async_step_theaters(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        venues = venues_in(self._venues, self._city or "")
        if user_input is not None:
            data = {CONF_CITY: self._city, **_clean(user_input)}
            names = [v.get("name") for v in venues if v.get("id") in data[CONF_VENUES]]
            await self.async_set_unique_id(f"{self._city}|{','.join(data[CONF_VENUES])}")
            self._abort_if_unique_id_configured()
            title = f"Cineville {self._city}"
            if names:
                title += f" ({', '.join(names[:3])}{'…' if len(names) > 3 else ''})"
            return self.async_create_entry(title=title, data=data)
        return self.async_show_form(
            step_id="theaters",
            data_schema=_settings_schema(venues, {}),
            description_placeholders={"city": self._city or "", "count": str(len(venues))},
        )

    @staticmethod
    @callback
    def async_get_options_flow(config_entry: ConfigEntry) -> OptionsFlow:
        return CinevilleOptionsFlow()


class CinevilleOptionsFlow(OptionsFlow):
    async def async_step_init(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        if user_input is not None:
            return self.async_create_entry(data=_clean(user_input))
        try:
            venues = venues_in(await CinevilleApi(async_get_clientsession(self.hass)).venues(),
                               self.config_entry.data[CONF_CITY])
        except CinevilleError:
            return self.async_abort(reason="cannot_connect")
        current = {**self.config_entry.data, **self.config_entry.options}
        return self.async_show_form(
            step_id="init",
            data_schema=_settings_schema(venues, current),
            description_placeholders={"city": self.config_entry.data[CONF_CITY]},
        )
