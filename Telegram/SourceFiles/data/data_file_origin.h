/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/variant.h"
#include "data/data_types.h"

namespace Data {

using FileOriginMessage = FullMsgId;

struct FileOriginUserPhoto {
	FileOriginUserPhoto(UserId userId, PhotoId photoId)
	: userId(userId)
	, photoId(photoId) {
	}

	UserId userId = 0;
	PhotoId photoId = 0;

	inline bool operator<(const FileOriginUserPhoto &other) const {
		return std::tie(userId, photoId)
			< std::tie(other.userId, other.photoId);
	}
};

struct FileOriginPeerPhoto {
	explicit FileOriginPeerPhoto(PeerId peerId) : peerId(peerId) {
	}

	PeerId peerId = 0;

	inline bool operator<(const FileOriginPeerPhoto &other) const {
		return peerId < other.peerId;
	}
};

struct FileOriginStickerSet {
	FileOriginStickerSet(uint64 setId, uint64 accessHash)
	: setId(setId)
	, accessHash(accessHash) {
	}

	uint64 setId = 0;
	uint64 accessHash = 0;

	inline bool operator<(const FileOriginStickerSet &other) const {
		return setId < other.setId;
	}
};

struct FileOriginSavedGifs {
	inline bool operator<(const FileOriginSavedGifs &) const {
		return false;
	}
};

struct FileOriginWallpaper {
	FileOriginWallpaper(uint64 paperId, uint64 accessHash)
	: paperId(paperId)
	, accessHash(accessHash) {
	}

	uint64 paperId = 0;
	uint64 accessHash = 0;

	inline bool operator<(const FileOriginWallpaper &other) const {
		return paperId < other.paperId;
	}
};

struct FileOrigin {
	using Variant = base::optional_variant<
		FileOriginMessage,
		FileOriginUserPhoto,
		FileOriginPeerPhoto,
		FileOriginStickerSet,
		FileOriginSavedGifs,
		FileOriginWallpaper>;

	FileOrigin() = default;
	FileOrigin(FileOriginMessage data) : data(data) {
	}
	FileOrigin(FileOriginUserPhoto data) : data(data) {
	}
	FileOrigin(FileOriginPeerPhoto data) : data(data) {
	}
	FileOrigin(FileOriginStickerSet data) : data(data) {
	}
	FileOrigin(FileOriginSavedGifs data) : data(data) {
	}
	FileOrigin(FileOriginWallpaper data) : data(data) {
	}

	explicit operator bool() const {
		return data.has_value();
	}
	inline bool operator<(const FileOrigin &other) const {
		return data < other.data;
	}

	Variant data;
};

struct DocumentFileLocationId {
	uint64 id = 0;
};

inline bool operator<(DocumentFileLocationId a, DocumentFileLocationId b) {
	return a.id < b.id;
}

struct PhotoFileLocationId {
	uint64 id = 0;
};

inline bool operator<(PhotoFileLocationId a, PhotoFileLocationId b) {
	return a.id < b.id;
}

using FileLocationId = base::variant<
	DocumentFileLocationId,
	PhotoFileLocationId>;

struct UpdatedFileReferences {
	std::map<FileLocationId, QByteArray> data;
};

UpdatedFileReferences GetFileReferences(const MTPmessages_Messages &data);
UpdatedFileReferences GetFileReferences(const MTPphotos_Photos &data);
UpdatedFileReferences GetFileReferences(
	const MTPmessages_RecentStickers &data);
UpdatedFileReferences GetFileReferences(
	const MTPmessages_FavedStickers &data);
UpdatedFileReferences GetFileReferences(const MTPmessages_StickerSet &data);
UpdatedFileReferences GetFileReferences(const MTPmessages_SavedGifs &data);
UpdatedFileReferences GetFileReferences(const MTPWallPaper &data);

} // namespace Data
