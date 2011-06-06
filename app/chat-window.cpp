/*
    Copyright (C) 2010  David Edmundson <kde@davidedmundson.co.uk>
    Copyright (C) 2011  Dominik Schmidt <dev@dominik-schmidt.de>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "chat-window.h"

#include "chat-search-bar.h"
#include "chat-tab.h"

#include <KStandardAction>
#include <KIcon>
#include <KLocale>
#include <KApplication>
#include <KAction>
#include <KActionCollection>
#include <KDebug>
#include <KIcon>
#include <KColorScheme>
#include <KTabBar>
#include <KSettings/Dialog>
#include <KNotifyConfigWidget>
#include <KMenuBar>
#include <KLineEdit>

#include <TelepathyQt4/Account>
#include <TelepathyQt4/TextChannel>

ChatWindow::ChatWindow()
{
    //setup actions
    KStandardAction::close(this,SLOT(closeCurrentTab()),actionCollection());
    KStandardAction::quit(KApplication::instance(), SLOT(quit()), actionCollection());
    KStandardAction::preferences(this, SLOT(showSettingsDialog()), actionCollection());
    KStandardAction::configureNotifications(this, SLOT(showNotificationsDialog()), actionCollection());
    KStandardAction::showMenubar(this->menuBar(), SLOT(setVisible(bool)), actionCollection());

    // keyboard shortcuts for the search bar
    KStandardAction::find(this, SLOT(onSearchActionToggled()), actionCollection());
    // start disabled
    KStandardAction::findNext(this, SLOT(onFindNextText()), actionCollection())->setEnabled(false);
    KStandardAction::findPrev(this, SLOT(onFindPreviousText()), actionCollection())->setEnabled(false);

    KAction *nextTabAction = new KAction(KIcon("go-next-view"), i18n("&Next Tab"), this);
    nextTabAction->setShortcuts(KStandardShortcut::tabNext());
    connect(nextTabAction, SIGNAL(triggered()), this, SLOT(onNextTabActionToggled()));

    KAction *previousTabAction = new KAction(KIcon("go-previous-view"), i18n("&Previous Tab"), this);
    previousTabAction->setShortcuts(KStandardShortcut::tabPrev());
    connect(previousTabAction, SIGNAL(triggered()), this, SLOT(onPreviousTabActionToggled()));

    // add custom actions to the collection
    actionCollection()->addAction("next-tab", nextTabAction);
    actionCollection()->addAction("previous-tab", previousTabAction);

    // set up m_tabWidget
    m_tabWidget = new KTabWidget(this);
    m_tabWidget->setTabReorderingEnabled(true);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setCloseButtonEnabled(true);
    m_tabWidget->setHoverCloseButtonDelayed(true);
    m_tabWidget->setTabBarHidden(true);
    connect(m_tabWidget, SIGNAL(closeRequest(QWidget*)), this, SLOT(removeTab(QWidget*)));
    connect(m_tabWidget, SIGNAL(currentChanged(int)), this, SLOT(onCurrentIndexChanged(int)));
    connect(qobject_cast<KTabBar*>(m_tabWidget->tabBar()), SIGNAL(mouseMiddleClick(int)),
                m_tabWidget, SLOT(removeTab(int)));

    setCentralWidget(m_tabWidget);

    setupGUI(QSize(460, 440), static_cast<StandardWindowOptions>(Default^StatusBar), "chatwindow.rc");
}

ChatWindow::~ChatWindow()
{
}

void ChatWindow::startChat(const Tp::TextChannelPtr &incomingTextChannel, const Tp::AccountPtr &account)
{
    // if targetHandle is None, targetId is also "", so create new chat
    if (incomingTextChannel->targetHandleType() == Tp::HandleTypeNone) {
        kDebug() << "ChatWindow::startChat target handle type is HandleTypeNone";
        createNewChat(incomingTextChannel, account);
        return;
    }

    bool duplicateTab = false;

    // check that the tab requested isn't already open
    for (int index = 0; index < m_tabWidget->count() && !duplicateTab; ++index) {

        // get chatWidget object
        ChatTab *auxChatTab = qobject_cast<ChatTab*>(m_tabWidget->widget(index));

        // this should never happen
        if (!auxChatTab) {
            return;
        }

        // check for 1on1 duplicate chat
        if (auxChatTab->textChannel()->targetId() == incomingTextChannel->targetId()
        && auxChatTab->textChannel()->targetHandleType() == incomingTextChannel->targetHandleType()) {
            duplicateTab = true;
            m_tabWidget->setCurrentIndex(index);    // set focus on selected tab

            // check if channel is invalid. Replace only if invalid
            // You get this status if user goes offline and then back on without closing the chat
            if (!auxChatTab->textChannel()->isValid()) {
                auxChatTab->setTextChannel(incomingTextChannel);    // replace with new one
                auxChatTab->setChatEnabled(true);                   // re-enable chat
            }
        } else if (auxChatTab->textChannel()->targetId() == incomingTextChannel->targetId()
          && auxChatTab->textChannel()->targetHandleType() == Tp::HandleTypeContact) {
            // got duplicate group chat. Wait for group handling to be sorted out
            ///TODO sort this out once group chats are supported
            kDebug() << "ChatWindow::startChat TODO need to implement when group chat is supported";
        }
    }

    // got new chat, create it
    if (!duplicateTab) {
        createNewChat(incomingTextChannel, account);
    }
}

void ChatWindow::removeTab(QWidget* chatWidget)
{
    m_tabWidget->removePage(chatWidget);
    delete chatWidget;
}

void ChatWindow::setTabText(int index, const QString &newTitle)
{
    m_tabWidget->setTabText(index, newTitle);

    // this updates the window title and icon if the updated tab is the current one
    if (index == m_tabWidget->currentIndex()) {
        onCurrentIndexChanged(index);
    }
}

void ChatWindow::setTabIcon(int index, const KIcon & newIcon)
{
    m_tabWidget->setTabIcon(index, newIcon);

    // this updates the window title and icon if the updated tab is the current one
    if (index == m_tabWidget->currentIndex()) {
        onCurrentIndexChanged(index);
    }
}

void ChatWindow::setTabTextColor(int index, const QColor& color)
{
    m_tabWidget->setTabTextColor(index, color);
}

void ChatWindow::closeCurrentTab()
{
    removeTab(m_tabWidget->currentWidget());
}

void ChatWindow::onCurrentIndexChanged(int index)
{
    kDebug() << index;

    if(index == -1) {
        close();
        return;
    }

    ChatTab* currentChatTab = qobject_cast<ChatTab*>(m_tabWidget->widget(index));
    setWindowTitle(currentChatTab->title());
    setWindowIcon(currentChatTab->icon());

    // when the tab changes I need to "refresh" the window's findNext and findPrev actions
    if (currentChatTab->chatSearchBar()->searchBar()->text().isEmpty()) {
        onEnableSearchActions(false);
    } else {
        onEnableSearchActions(true);
    }
}

void ChatWindow::onEnableSearchActions(bool enable)
{
    actionCollection()->action(KStandardAction::name(KStandardAction::FindNext))->setEnabled(enable);
    actionCollection()->action(KStandardAction::name(KStandardAction::FindPrev))->setEnabled(enable);
}

void ChatWindow::onFindNextText()
{
    ChatTab *currChat = qobject_cast<ChatTab*>(m_tabWidget->currentWidget());

    // This should never happen
    if(!currChat) {
        return;
    }
    currChat->chatSearchBar()->onNextButtonClicked();
}

void ChatWindow::onFindPreviousText()
{
    ChatTab *currChat = qobject_cast<ChatTab*>(m_tabWidget->currentWidget());

    // This should never happen
    if(!currChat) {
        return;
    }
    currChat->chatSearchBar()->onPreviousButtonClicked();
}

void ChatWindow::onNextTabActionToggled()
{
    int currIndex = m_tabWidget->currentIndex();

    if (currIndex < m_tabWidget->count() && m_tabWidget->count() != 1) {
        m_tabWidget->setCurrentIndex(++currIndex);
    }
}

void ChatWindow::onPreviousTabActionToggled()
{
    int currIndex = m_tabWidget->currentIndex();

    if (currIndex > 0) {
        m_tabWidget->setCurrentIndex(--currIndex);
    }
}

void ChatWindow::onSearchActionToggled()
{
    ChatTab *currChat = qobject_cast<ChatTab*>(m_tabWidget->currentWidget());

    // This should never happen
    if(!currChat) {
        return;
    }
    currChat->toggleSearchBar();
}

void ChatWindow::onTabStateChanged()
{
    kDebug();

    ChatTab* sender = qobject_cast<ChatTab*>(QObject::sender());
    if (sender) {
        int tabIndex = m_tabWidget->indexOf(sender);
        setTabTextColor(tabIndex, sender->titleColor());
    }
}

void ChatWindow::onTabIconChanged(const KIcon & newIcon)
{
    //find out which widget made the call, and update the correct tab.
    QWidget* sender = qobject_cast<QWidget*>(QObject::sender());
    if (sender) {
        int tabIndexToChange = m_tabWidget->indexOf(sender);
        setTabIcon(tabIndexToChange, newIcon);
    }
}

void ChatWindow::onTabTextChanged(const QString &newTitle)
{
    //find out which widget made the call, and update the correct tab.
    QWidget* sender = qobject_cast<QWidget*>(QObject::sender());
    if (sender) {
        int tabIndexToChange = m_tabWidget->indexOf(sender);
        setTabText(tabIndexToChange, newTitle);
    }
}

void ChatWindow::showSettingsDialog()
{
    kDebug();

    KSettings::Dialog *dialog = new KSettings::Dialog(this);

    dialog->addModule("kcm_telepathy_chat_config");
    dialog->addModule("kcm_telepathy_accounts");

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void ChatWindow::showNotificationsDialog()
{
    KNotifyConfigWidget::configure(this, "ktelepathy");
}

void ChatWindow::createNewChat(const Tp::TextChannelPtr &channelPtr, const Tp::AccountPtr &accountPtr)
{
    ChatTab *chatTab = new ChatTab(channelPtr, accountPtr, m_tabWidget);
    setupChatTabSignals(chatTab);
    chatTab->setTabWidget(m_tabWidget);
    m_tabWidget->addTab(chatTab, chatTab->icon(), chatTab->title());
    m_tabWidget->setCurrentWidget(chatTab);

    if (m_tabWidget->isTabBarHidden()) {
        if (m_tabWidget->count() > 1) {
            m_tabWidget->setTabBarHidden(false);
        }
    }
}

void ChatWindow::setupChatTabSignals(ChatTab *chatTab)
{
    connect(chatTab, SIGNAL(titleChanged(QString)), this, SLOT(onTabTextChanged(QString)));
    connect(chatTab, SIGNAL(iconChanged(KIcon)), this, SLOT(onTabIconChanged(KIcon)));
    connect(chatTab, SIGNAL(userTypingChanged(bool)), this, SLOT(onTabStateChanged()));
    connect(chatTab, SIGNAL(unreadMessagesChanged(int)), this, SLOT(onTabStateChanged()));
    connect(chatTab, SIGNAL(contactPresenceChanged(Tp::Presence)), this, SLOT(onTabStateChanged()));
    connect(chatTab->chatSearchBar(), SIGNAL(enableSearchButtonsSignal(bool)), this, SLOT(onEnableSearchActions(bool)));
}

#include "chat-window.moc"
