"""Two sensors per city: the films that play (state: how many), and the films that come soon."""

from __future__ import annotations

from typing import Any

from homeassistant.components.sensor import SensorEntity
from homeassistant.core import HomeAssistant
from homeassistant.helpers.device_registry import DeviceEntryType, DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity
from homeassistant.util import dt as dt_util

from . import CinevilleConfigEntry
from .const import ATTRIBUTION, DETAILED_FILMS, DOMAIN, WEBSITE
from .coordinator import CinevilleCoordinator
from .films import Film, compact

# Lists change with every update and are long: keep them out of the recorder's history.
_LISTS = frozenset({"films", "titles", "highlights", "directors", "shows", "shows_today", "next_start", "next_venue",
                    "premieres", "venues"})


async def async_setup_entry(hass: HomeAssistant, entry: CinevilleConfigEntry,
                            async_add_entities: AddEntitiesCallback) -> None:
    coordinator = entry.runtime_data
    async_add_entities([CinevilleSensor(coordinator, "now"), CinevilleSensor(coordinator, "soon")])


class CinevilleSensor(CoordinatorEntity[CinevilleCoordinator], SensorEntity):
    _attr_has_entity_name = True
    _attr_attribution = ATTRIBUTION
    _attr_native_unit_of_measurement = "films"
    _unrecorded_attributes = _LISTS

    def __init__(self, coordinator: CinevilleCoordinator, kind: str) -> None:
        super().__init__(coordinator)
        entry = coordinator.config_entry
        self._kind = kind
        self._attr_unique_id = f"{entry.entry_id}_{kind}"
        self._attr_translation_key = "now_showing" if kind == "now" else "coming_soon"
        self._attr_icon = "mdi:movie-open-star" if kind == "now" else "mdi:movie-open-plus"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.entry_id)},
            name=entry.title,
            manufacturer="Cineville",
            entry_type=DeviceEntryType.SERVICE,
            configuration_url=WEBSITE,
        )

    def _films(self) -> list[Film]:
        data = self.coordinator.data
        return (data.now if self._kind == "now" else data.soon) if data else []

    @property
    def native_value(self) -> int:
        return len(self._films())

    @property
    def extra_state_attributes(self) -> dict[str, Any]:
        data = self.coordinator.data
        if not data:
            return {}
        zone = dt_util.get_default_time_zone()
        films = self._films()
        attributes = compact(films, self._kind, zone)
        attributes["city"] = data.city
        attributes["venues"] = data.venues
        attributes["films"] = [f.detail(zone) for f in films[:DETAILED_FILMS]]
        return attributes
