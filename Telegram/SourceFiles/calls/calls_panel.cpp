/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "calls/calls_panel.h"

#include "calls/calls_emoji_fingerprint.h"
#include "styles/style_calls.h"
#include "styles/style_history.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/effects/ripple_animation.h"
#include "ui/widgets/shadow.h"
#include "messenger.h"
#include "lang.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "observer_peer.h"
#include "platform/platform_specific.h"
#include "base/task_queue.h"

namespace Calls {
namespace {

constexpr auto kTooltipShowTimeoutMs = 1000;

} // namespace

class Panel::Button : public Ui::RippleButton {
public:
	Button(QWidget *parent, const style::CallButton &st);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	const style::CallButton &_st;
	QPixmap _bg;

};

Panel::Button::Button(QWidget *parent, const style::CallButton &st) : Ui::RippleButton(parent, st.button.ripple)
, _st(st) {
	resize(_st.button.width, _st.button.height);
	_bg = App::pixmapFromImageInPlace(style::colorizeImage(prepareRippleMask(), _st.bg));
}

void Panel::Button::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.drawPixmap(myrtlpoint(_st.button.rippleAreaPosition), _bg);

	auto ms = getms();

	paintRipple(p, _st.button.rippleAreaPosition.x(), _st.button.rippleAreaPosition.y(), ms);

	auto down = isDown();
	auto position = _st.button.iconPosition;
	if (position.x() < 0) {
		position.setX((width() - _st.button.icon.width()) / 2);
	}
	if (position.y() < 0) {
		position.setY((height() - _st.button.icon.height()) / 2);
	}
	_st.button.icon.paint(p, position, width());
}

void Panel::Button::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);

	auto over = isOver();
	auto wasOver = static_cast<bool>(was & StateFlag::Over);
	if (over != wasOver) {
		update();
	}
}

QPoint Panel::Button::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _st.button.rippleAreaPosition;
}

QImage Panel::Button::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(QSize(_st.button.rippleAreaSize, _st.button.rippleAreaSize));
}

Panel::Panel(gsl::not_null<Call*> call)
: _call(call)
, _user(call->user())
, _mute(this, st::callMuteToggle)
, _name(this, st::callName)
, _status(this, st::callStatus) {
	setMouseTracking(true);
	initControls();
	initLayout();
	showAndActivate();
}

void Panel::showAndActivate() {
	show();
	raise();
	setWindowState(windowState() | Qt::WindowActive);
	activateWindow();
	setFocus();
}

void Panel::replaceCall(gsl::not_null<Call*> call) {
	_call = call;
	_user = call->user();
	reinitControls();
	updateControlsGeometry();
}

bool Panel::event(QEvent *e) {
	if (e->type() == QEvent::WindowDeactivate) {
		if (_call && _call->state() == State::Established) {
			hideDeactivated();
		}
	}
	return TWidget::event(e);
}

void Panel::hideDeactivated() {
	hide();
}

void Panel::initControls() {
	_mute->setClickedCallback([this] {
		if (_call) {
			_call->setMute(!_call->isMute());
		}
	});
	subscribe(_call->muteChanged(), [this](bool mute) {
		_mute->setIconOverride(mute ? &st::callUnmuteIcon : nullptr);
	});
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::NameChanged, [this](const Notify::PeerUpdate &update) {
		if (!_call || update.peer != _call->user()) {
			return;
		}
		_name->setText(App::peerName(_call->user()));
		updateControlsGeometry();
	}));
	_updateDurationTimer.setCallback([this] {
		if (_call) {
			updateStatusText(_call->state());
		}
	});

	reinitControls();
}

void Panel::reinitControls() {
	Expects(_call != nullptr);

	unsubscribe(_stateChangedSubscription);
	_stateChangedSubscription = subscribe(_call->stateChanged(), [this](State state) { stateChanged(state); });
	stateChanged(_call->state());

	_name->setText(App::peerName(_call->user()));
	updateStatusText(_call->state());
}

void Panel::refreshCallbacks() {
	auto safeSetCallback = [this](auto &&button, auto &&callback) {
		if (button) {
			button->setClickedCallback([this, callback] {
				if (_call) {
					callback(_call);
				}
			});
		};
	};
	safeSetCallback(_answer, [](gsl::not_null<Call*> call) { call->answer(); });
	safeSetCallback(_redial, [](gsl::not_null<Call*> call) { call->redial(); });
	safeSetCallback(_hangup, [](gsl::not_null<Call*> call) { call->hangup(); });
	safeSetCallback(_cancel, [](gsl::not_null<Call*> call) { call->hangup(); });
}

void Panel::initLayout() {
	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint) | Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint | Qt::NoDropShadowWindowHint | Qt::Dialog);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);

	initGeometry();

	processUserPhoto();
	subscribe(AuthSession::Current().api().fullPeerUpdated(), [this](PeerData *peer) {
		if (peer == _user) {
			processUserPhoto();
		}
	});
	subscribe(AuthSession::CurrentDownloaderTaskFinished(), [this] {
		refreshUserPhoto();
	});
	createDefaultCacheImage();
	toggleOpacityAnimation(true);

	Platform::InitOnTopPanel(this);
}

void Panel::toggleOpacityAnimation(bool visible) {
	if (_useTransparency) {
		if (_animationCache.isNull()) {
			_animationCache = myGrab(this);
			hideChildren();
		}
		_opacityAnimation.start([this] { update(); }, visible ? 0. : 1., visible ? 1. : 0., st::callPanelDuration, visible ? anim::easeOutCirc : anim::easeInCirc);
	}
}

void Panel::finishAnimation() {
	_animationCache = QPixmap();
	if (_call) {
		showChildren();
	} else {
		destroyDelayed();
	}
}

void Panel::destroyDelayed() {
	hide();
	base::TaskQueue::Main().Put([weak = QPointer<Panel>(this)] {
		if (weak) {
			delete weak.data();
		}
	});
}

void Panel::hideAndDestroy() {
	toggleOpacityAnimation(false);
	_call = nullptr;
	if (_animationCache.isNull()) {
		destroyDelayed();
	}
}

void Panel::processUserPhoto() {
	if (!_user->userpicLoaded()) {
		_user->loadUserpic(true);
	}
	auto photo = (_user->photoId && _user->photoId != UnknownPeerPhotoId) ? App::photo(_user->photoId) : nullptr;
	if (isGoodUserPhoto(photo)) {
		photo->full->load(true);
	} else {
		if ((_user->photoId == UnknownPeerPhotoId) || (_user->photoId && (!photo || !photo->date))) {
			App::api()->requestFullPeer(_user);
		}
	}
	refreshUserPhoto();
}

void Panel::refreshUserPhoto() {
	auto photo = (_user->photoId && _user->photoId != UnknownPeerPhotoId) ? App::photo(_user->photoId) : nullptr;
	if (isGoodUserPhoto(photo) && photo->full->loaded() && (photo->id != _userPhotoId || !_userPhotoFull)) {
		_userPhotoId = photo->id;
		_userPhotoFull = true;
		createUserpicCache(photo->full);
	} else if (_userPhoto.isNull()) {
		if (auto userpic = _user->currentUserpic()) {
			createUserpicCache(userpic);
		}
	}
}

void Panel::createUserpicCache(ImagePtr image) {
	auto size = st::callWidth * cIntRetinaFactor();
	auto options = _useTransparency ? (Images::Option::RoundedLarge | Images::Option::RoundedTopLeft | Images::Option::RoundedTopRight | Images::Option::Smooth) : Images::Option::None;
	auto width = image->width();
	auto height = image->height();
	if (width > height) {
		width = qMax((width * size) / height, 1);
		height = size;
	} else {
		height = qMax((height * size) / width, 1);
		width = size;
	}
	_userPhoto = image->pixNoCache(width, height, options, st::callWidth, st::callWidth);
	if (cRetina()) _userPhoto.setDevicePixelRatio(cRetinaFactor());

	refreshCacheImageUserPhoto();

	update();
}

bool Panel::isGoodUserPhoto(PhotoData *photo) {
	if (!photo || !photo->date) {
		return false;
	}
	auto badAspect = [](int a, int b) {
		return a > 10 * b;
	};
	auto width = photo->full->width();
	auto height = photo->full->height();
	return !badAspect(width, height) && !badAspect(height, width);
}

void Panel::initGeometry() {
	auto center = Messenger::Instance().getPointForCallPanelCenter();
	_useTransparency = Platform::TranslucentWindowsSupported(center);
	setAttribute(Qt::WA_OpaquePaintEvent, !_useTransparency);
	_padding = _useTransparency ? st::callShadow.extend : style::margins(st::lineWidth, st::lineWidth, st::lineWidth, st::lineWidth);
	_contentTop = _padding.top() + st::callWidth;
	auto screen = QApplication::desktop()->screenGeometry(center);
	auto rect = QRect(0, 0, st::callWidth, st::callHeight);
	setGeometry(rect.translated(center - rect.center()).marginsAdded(_padding));
	createBottomImage();
	updateControlsGeometry();
}

void Panel::createBottomImage() {
	if (!_useTransparency) {
		return;
	}
	auto bottomWidth = width();
	auto bottomHeight = height() - _padding.top() - st::callWidth;
	auto image = QImage(QSize(bottomWidth, bottomHeight) * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	{
		Painter p(&image);
		Ui::Shadow::paint(p, QRect(_padding.left(), 0, st::callWidth, bottomHeight - _padding.bottom()), width(), st::callShadow, Ui::Shadow::Side::Left | Ui::Shadow::Side::Right | Ui::Shadow::Side::Bottom);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(st::callBg);
		p.setPen(Qt::NoPen);
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(myrtlrect(_padding.left(), -st::historyMessageRadius, st::callWidth, bottomHeight - _padding.bottom() + st::historyMessageRadius), st::historyMessageRadius, st::historyMessageRadius);
	}
	_bottomCache = App::pixmapFromImageInPlace(std::move(image));
}

void Panel::createDefaultCacheImage() {
	if (!_useTransparency || !_cache.isNull()) {
		return;
	}
	auto cache = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(cRetinaFactor());
	cache.fill(Qt::transparent);
	{
		Painter p(&cache);
		auto inner = rect().marginsRemoved(_padding);
		Ui::Shadow::paint(p, inner, width(), st::callShadow);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(st::callBg);
		p.setPen(Qt::NoPen);
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(myrtlrect(inner), st::historyMessageRadius, st::historyMessageRadius);
	}
	_cache = App::pixmapFromImageInPlace(std::move(cache));
}

void Panel::refreshCacheImageUserPhoto() {
	auto cache = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(cRetinaFactor());
	cache.fill(Qt::transparent);
	{
		Painter p(&cache);
		Ui::Shadow::paint(p, QRect(_padding.left(), _padding.top(), st::callWidth, st::callWidth), width(), st::callShadow, Ui::Shadow::Side::Top | Ui::Shadow::Side::Left | Ui::Shadow::Side::Right);
		p.drawPixmapLeft(_padding.left(), _padding.top(), width(), _userPhoto);
		p.drawPixmapLeft(0, _padding.top() + st::callWidth, width(), _bottomCache);
	}
	_cache = App::pixmapFromImageInPlace(std::move(cache));
}

void Panel::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void Panel::updateControlsGeometry() {
	_name->moveToLeft((width() - _name->width()) / 2, _contentTop + st::callNameTop);
	updateStatusGeometry();

	auto controlsTop = _contentTop + st::callControlsTop;
	if (_answer || _redial) {
		auto bothWidth = (_answer ? _answer : _redial)->width() + st::callControlsSkip + (_hangup ? _hangup : _cancel)->width();
		if (_hangup) _hangup->moveToLeft((width() - bothWidth) / 2, controlsTop);
		if (_cancel) _cancel->moveToLeft((width() - bothWidth) / 2, controlsTop);
		if (_answer) _answer->moveToRight((width() - bothWidth) / 2, controlsTop);
		if (_redial) _redial->moveToRight((width() - bothWidth) / 2, controlsTop);
	} else {
		t_assert(_hangup != nullptr);
		_hangup->moveToLeft((width() - _hangup->width()) / 2, controlsTop);
	}
	_mute->moveToRight(_padding.right() + st::callMuteRight, controlsTop);
}

void Panel::updateStatusGeometry() {
	_status->moveToLeft((width() - _status->width()) / 2, _contentTop + st::callStatusTop);
}

void Panel::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (!_animationCache.isNull()) {
		auto opacity = _opacityAnimation.current(getms(), _call ? 1. : 0.);
		if (!_opacityAnimation.animating()) {
			finishAnimation();
			if (!_call) return;
		} else {
			p.setOpacity(opacity);

			PainterHighQualityEnabler hq(p);
			auto marginRatio = (1. - opacity) / 5;
			auto marginWidth = qRound(width() * marginRatio);
			auto marginHeight = qRound(height() * marginRatio);
			p.drawPixmap(rect().marginsRemoved(QMargins(marginWidth, marginHeight, marginWidth, marginHeight)), _animationCache, QRect(QPoint(0, 0), _animationCache.size()));
			return;
		}
	}

	if (_useTransparency) {
		Platform::StartTranslucentPaint(p, e);
		p.drawPixmapLeft(0, 0, width(), _cache);
	} else {
		p.drawPixmapLeft(_padding.left(), _padding.top(), width(), _userPhoto);
		auto callBgOpaque = st::callBg->c;
		callBgOpaque.setAlpha(255);
		auto brush = QBrush(callBgOpaque);
		p.fillRect(0, 0, width(), _padding.top(), brush);
		p.fillRect(myrtlrect(0, _padding.top(), _padding.left(), _contentTop - _padding.top()), brush);
		p.fillRect(myrtlrect(width() - _padding.right(), _padding.top(), _padding.right(), _contentTop - _padding.top()), brush);
		p.fillRect(0, _contentTop, width(), height() - _contentTop, brush);
	}

	if (!_fingerprint.empty()) {
		App::roundRect(p, _fingerprintArea, st::callFingerprintBg, ImageRoundRadius::Small);

		auto realSize = Ui::Emoji::Size(Ui::Emoji::Index() + 1);
		auto size = realSize / cIntRetinaFactor();
		auto left = _fingerprintArea.left() + st::callFingerprintPadding.left();
		auto top = _fingerprintArea.top() + st::callFingerprintPadding.top();
		for (auto emoji : _fingerprint) {
			p.drawPixmap(QPoint(left, top), App::emojiLarge(), QRect(emoji->x() * realSize, emoji->y() * realSize, realSize, realSize));
			left += st::callFingerprintSkip + size;
		}
	}
}

void Panel::mousePressEvent(QMouseEvent *e) {
	auto dragArea = myrtlrect(_padding.left(), _padding.top(), st::callWidth, st::callWidth);
	if (e->button() == Qt::LeftButton) {
		if (dragArea.contains(e->pos())) {
			_dragging = true;
			_dragStartMousePosition = e->globalPos();
			_dragStartMyPosition = QPoint(x(), y());
		} else if (!rect().contains(e->pos())) {
			if (_call && _call->state() == State::Established) {
				hideDeactivated();
			}
		}
	}
}

void Panel::mouseMoveEvent(QMouseEvent *e) {
	if (_dragging) {
		Ui::Tooltip::Hide();
		if (!(e->buttons() & Qt::LeftButton)) {
			_dragging = false;
		} else {
			move(_dragStartMyPosition + (e->globalPos() - _dragStartMousePosition));
		}
	} else if (_fingerprintArea.contains(e->pos())) {
		Ui::Tooltip::Show(kTooltipShowTimeoutMs, this);
	} else {
		Ui::Tooltip::Hide();
	}
}

void Panel::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_dragging = false;
	}
}

void Panel::leaveEventHook(QEvent *e) {
	Ui::Tooltip::Hide();
}

void Panel::leaveToChildEvent(QEvent *e, QWidget *child) {
	Ui::Tooltip::Hide();
}

QString Panel::tooltipText() const {
	return lng_call_fingerprint_tooltip(lt_user, App::peerName(_user));
}

QPoint Panel::tooltipPos() const {
	return QCursor::pos();
}

bool Panel::tooltipWindowActive() const {
	return !isHidden();
}

void Panel::stateChanged(State state) {
	updateStatusText(state);

	auto buttonsUpdated = false;
	auto syncButton = [this, &buttonsUpdated](auto &&button, bool exists, auto &&style) {
		if (exists == (button != nullptr)) {
			return;
		}
		if (exists) {
			button.create(this, style);
			button->show();
		} else {
			button.destroy();
		}
		buttonsUpdated = true;
	};
	if (_call) {
		syncButton(_answer, (_call->type() == Call::Type::Incoming) && ((state == State::Starting) || (state == State::WaitingIncoming)), st::callAnswer);
		syncButton(_hangup, (state != State::Busy), st::callHangup);
		syncButton(_redial, (state == State::Busy), st::callAnswer);
		syncButton(_cancel, (state == State::Busy), st::callCancel);

		if (_fingerprint.empty() && _call->isKeyShaForFingerprintReady()) {
			fillFingerprint();
		}
	}
	if (buttonsUpdated) {
		refreshCallbacks();
		updateControlsGeometry();
	}

	if ((state == State::Starting) || (state == State::WaitingIncoming)) {
		Platform::ReInitOnTopPanel(this);
	} else {
		Platform::DeInitOnTopPanel(this);
	}
	if (state == State::Established) {
		if (!isActiveWindow()) {
			hideDeactivated();
		}
	}
}

void Panel::fillFingerprint() {
	Expects(_call != nullptr);
	_fingerprint = ComputeEmojiFingerprint(_call);

	auto realSize = Ui::Emoji::Size(Ui::Emoji::Index() + 1);
	auto size = realSize / cIntRetinaFactor();
	auto count = _fingerprint.size();
	auto rectWidth = count * size + (count - 1) * st::callFingerprintSkip;
	auto rectHeight = size;
	auto left = (width() - rectWidth) / 2;
	auto top = _contentTop - st::callFingerprintBottom - st::callFingerprintPadding.bottom() - size;
	_fingerprintArea = QRect(left, top, rectWidth, rectHeight).marginsAdded(st::callFingerprintPadding);

	update();
}

void Panel::updateStatusText(State state) {
	auto statusText = [this, state]() -> QString {
		switch (state) {
		case State::Starting:
		case State::WaitingInit:
		case State::WaitingInitAck: return lang(lng_call_status_connecting);
		case State::Established: {
			if (_call) {
				auto durationMs = _call->getDurationMs();
				auto durationSeconds = durationMs / 1000;
				startDurationUpdateTimer(durationMs);
				return formatDurationText(durationSeconds);
			}
			return lang(lng_call_status_ended);
		} break;
		case State::Failed: return lang(lng_call_status_failed);
		case State::HangingUp: return lang(lng_call_status_hanging);
		case State::Ended: return lang(lng_call_status_ended);
		case State::ExchangingKeys: return lang(lng_call_status_exchanging);
		case State::Waiting: return lang(lng_call_status_waiting);
		case State::Requesting: return lang(lng_call_status_requesting);
		case State::WaitingIncoming: return lang(lng_call_status_incoming);
		case State::Ringing: return lang(lng_call_status_ringing);
		case State::Busy: return lang(lng_call_status_busy);
		}
		Unexpected("State in stateChanged()");
	};
	_status->setText(statusText());
	updateStatusGeometry();
}

void Panel::startDurationUpdateTimer(TimeMs currentDuration) {
	auto msTillNextSecond = 1000 - (currentDuration % 1000);
	_updateDurationTimer.callOnce(msTillNextSecond + 5);
}

} // namespace Calls