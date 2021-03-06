/*
* Copyright (c) 2012, Eunhyuk Kim(gunoodaddy) 
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
*   * Redistributions of source code must retain the above copyright notice,
*     this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in the
*     documentation and/or other materials provided with the distribution.
*   * Neither the name of Redis nor the names of its contributors may be used
*     to endorse or promote products derived from this software without
*     specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include "StdAfx.h"
#include "sharedpainter.h"
#include "TextItemDialog.h"
#include "AboutWindow.h"
#include "UIStyleSheet.h"
#include "PreferencesDialog.h"
#include "JoinerListWindow.h"

static const int DEFAULT_HIDE_POS_X = 9999;
static const int DEFAULT_HIDE_POS_Y = 9999;

#define ADD_CHAT_VERTICAL_SPACE(space)	\
{	\
	QTextCharFormat fmt;	\
	fmt.setFontPointSize( space );	\
	ui.editChat->setCurrentCharFormat( fmt );	\
	ui.editChat->append( "" );	\
}

#define ADD_CHAT_VERTICAL_SPACE_CHAT_SMALL()	ADD_CHAT_VERTICAL_SPACE(1)
#define ADD_CHAT_VERTICAL_SPACE_CHAT_BIG()	ADD_CHAT_VERTICAL_SPACE(3)

SharedPainter::SharedPainter(CSharedPainterScene *canvas, QWidget *parent, Qt::WFlags flags)
	: QMainWindow(parent, flags), canvas_(canvas), enabledPainter_(true), modifiedFlag_(false), currPacketId_(-1)
	, changeScrollPosFreezingFlag_(false), resizeFreezingFlag_(false), resizeSplitterFreezingFlag_(false), playbackSliderFreezingFlag_(false)
	, screenShotMode_(false), exitFlag_(false), wroteProgressBar_(NULL)
	, lastTextPosX_(0), lastTextPosY_(0), status_(INIT), findingServerWindow_(NULL), syncProgressWindow_(NULL)
{
	CSingleton<CUpgradeManager>::Instance();

	std::string myId = SettingManagerPtr()->myId();
	if( myId.empty() )
	{
		myId = Util::generateMyId();
		SettingManagerPtr()->setMyId( myId );
	}

	SharePaintManagerPtr()->initialize( myId );

	fontBroadCastText_ = QFont( "Times" );
	fontBroadCastText_.setBold( true );
	fontBroadCastText_.setPixelSize( 20 );

	ui.setupUi(this);
	connect( ui.splitter, SIGNAL(splitterMoved(int, int)), this, SLOT(splitterMoved(int, int)));
	ui.painterView->setScene( canvas );
	ui.painterView->setRenderHints( QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::HighQualityAntialiasing | QPainter::SmoothPixmapTransform );

	QScrollBar *scrollBarH = ui.painterView->horizontalScrollBar();
	QScrollBar *scrollBarV = ui.painterView->verticalScrollBar();
	connect( scrollBarH, SIGNAL(valueChanged(int)), this, SLOT(onPaintViewHScrollBarChanged(int)) );
	connect( scrollBarV, SIGNAL(valueChanged(int)), this, SLOT(onPaintViewVScrollBarChanged(int)) );

	ui.editChat->document()->setDefaultStyleSheet(gStyleSheet_Chat);
	ui.editChat->setReadOnly( true );
	ui.editMsg->installEventFilter( this );
	ui.recodingIndicator->hide();

	// test
	//UpgradeWindow wnd;
	//wnd.setContents( VERSION_TEXT, Util::toStringFromUtf8( "test123\ntest222" ) );
	//wnd.exec();

	setCursor( Qt::ArrowCursor ); 

	canvas_->setEvent( this );

	UpgradeManagerPtr()->registerObserver( this );
	SharePaintManagerPtr()->registerObserver( this );
	SharePaintManagerPtr()->setCanvas( canvas_ );
	screenRecoder_.registerObserver(this);
	
	QMenuBar *menuBar = ui.menuBar;

	// Create Menu bar item
	{
		// File Menu
		QMenu* file = new QMenu( "&File", menuBar );
		file->addAction( "&Import from file", this, SLOT(actionImportFile()), Qt::CTRL+Qt::Key_I );
		file->addAction( "&Export to file", this, SLOT(actionExportFile()),  Qt::CTRL+Qt::Key_E );
		file->addAction( "&Save image", this, SLOT(actionSaveImageFile()),  Qt::CTRL+Qt::Key_S );
		file->addSeparator();
		file->addAction( "&About", this, SLOT(actionAbout()) );
		file->addSeparator();
		file->addAction( "E&xit", this, SLOT(actionExit()), Qt::CTRL+Qt::Key_Q );
		menuBar->addMenu( file );

		// Edit Menu
		QMenu* edit = new QMenu( "&Edit", menuBar );
		QMenu* penMenu = edit->addMenu( "Pen Setting" );
		penWidthAction_ = penMenu->addAction( "Pen &Width", this, SLOT(actionPenWidth()), Qt::ALT+Qt::Key_V );
		penMenu->addAction( "Pen &Color", this, SLOT(actionPenColor()), Qt::ALT+Qt::Key_C );
		penModeAction_ = edit->addAction( "Pen Mode", this, SLOT(actionPenMode()), Qt::Key_A );
		edit->addAction( "&Text", this, SLOT(actionAddText()), Qt::Key_Enter|Qt::Key_Return );
		gridLineAction_ = edit->addAction( "&Draw Grid Line", this, SLOT(actionGridLine()));
		edit->addAction( "&Background Color", this, SLOT(actionBGColor()), Qt::ALT+Qt::Key_B );
		edit->addAction( "&Screen Shot", this, SLOT(actionScreenShot()), Qt::ALT+Qt::Key_S );
		edit->addAction( "&Screen Record", this, SLOT(actionScreenRecord()), Qt::ALT+Qt::Key_R );
		edit->addAction( "&Screen UDP Streaming", this, SLOT(actionScreenStreaming()) );
		edit->addSeparator();
		edit->addAction( "Clear &Background", this, SLOT(actionClearBG()), Qt::CTRL+Qt::Key_B );
		edit->addAction( "Cl&ear Screen", this, SLOT(actionClearScreen()), Qt::CTRL+Qt::Key_X );
		edit->addSeparator();
		edit->addAction( "&Paste clipboard", this, SLOT(actionClipboardPaste()), Qt::CTRL+Qt::Key_V );
		edit->addSeparator();
		edit->addAction( "&Undo", this, SLOT(actionUndo()), Qt::CTRL+Qt::Key_Z );
		edit->addAction( "&Redo", this, SLOT(actionRedo()), Qt::CTRL+Qt::SHIFT+Qt::Key_Z );
		menuBar->addMenu( edit );

		// Network Menu
		QMenu* network = new QMenu( "&Network", menuBar );
		network->addAction( "&Connect to Relay Server", this, SLOT(actionConnectServer()) );
		network->addAction( "&Connect to Peer", this, SLOT(actionConnect()) );
		network->addSeparator();
		startFindServerAction_ = network->addAction( "Start &Find Server", this, SLOT(actionFindingServer()), Qt::CTRL+Qt::Key_1 );
		network->addSeparator();
		network->addAction( "Broadcast &Text Message", this, SLOT(actionBroadcastTextMessage()), Qt::CTRL+Qt::Key_M );
		network->addSeparator();
		network->addAction( "Close all connections", this, SLOT(actionCloseConnection()) );
		menuBar->addMenu( network );
		
		//  Menu
		QMenu* options = new QMenu( "&Options", menuBar );
		options->addAction( "&Nick Name", this, SLOT(actionNickName()) );
		options->addAction( "&Paint Channel", this, SLOT(actionPaintChannel()), Qt::CTRL+Qt::Key_H );
		options->addSeparator();
		options->addAction( "&Preferences", this, SLOT(actionPreferences()), Qt::CTRL+Qt::Key_P  );
		menuBar->addMenu( options );

		gridLineAction_->setCheckable( true );
		penModeAction_->setCheckable( true );
	}


	// create tool bar
	{
		ui.toolBar->setIconSize( QSize(32, 32) );
		toolBar_penColorButton_ = new QPushButton();
		toolBar_penColorButton_->connect( toolBar_penColorButton_, SIGNAL(clicked()), this, SLOT(actionPenColor()) );
		toolBar_penColorButton_->setToolTip( tr("Pen Color") );

		toolBar_bgColorButton_ = new QPushButton();
		toolBar_bgColorButton_->connect( toolBar_bgColorButton_, SIGNAL(clicked()), this, SLOT(actionBGColor()) );
		toolBar_bgColorButton_->setToolTip( tr("Background Color") );

		toolBar_MoveMode_ = ui.toolBar->addAction( QIcon(":/SharedPainter/Resources/move_mode.png"), "Move", this, SLOT(actionMoveMode()) );
		toolBar_PenMode_ = ui.toolBar->addAction( QIcon(":/SharedPainter/Resources/draw_line.png"), "Pen", this, SLOT(actionFreePenMode()) );
		ui.toolBar->addSeparator();
		ui.toolBar->addWidget( toolBar_penColorButton_ );
		
		QToolButton *penWidthButton = new QToolButton();
		//penWidthButton->setArrowType( Qt::NoArrow );
		QMenu *menuPenWidth = new QMenu();
		menuPenWidth->addAction( "Pen Width 20", this, SLOT(actionPenWidth20()) );
		menuPenWidth->addAction( "Pen Width 10", this, SLOT(actionPenWidth10()) );
		menuPenWidth->addAction( "Pen Width 6", this, SLOT(actionPenWidth6()) );
		menuPenWidth->addAction( "Pen Width 3", this, SLOT(actionPenWidth3()) );
		menuPenWidth->addAction( "Pen Width 1", this, SLOT(actionPenWidth1()) );
		menuPenWidth->addAction( penWidthAction_ );
		penWidthButton->setIcon( QIcon(":/SharedPainter/Resources/pen_width.png") );
		penWidthButton->setMenu( menuPenWidth );
		penWidthButton->connect( penWidthButton, SIGNAL(clicked()), penWidthButton, SLOT(showMenu()) );
		ui.toolBar->addWidget( penWidthButton );

		ui.toolBar->addSeparator();
		ui.toolBar->addAction( QIcon(":/SharedPainter/Resources/screenshot.png"), "Screen Shot", this, SLOT(actionScreenShot()) );
		ui.toolBar->addWidget( toolBar_bgColorButton_ );
		toolBar_GridLine_ = ui.toolBar->addAction( QIcon(":/SharedPainter/Resources/grid_line.png"), "Grid Line", this, SLOT(actionGridLine()) );
		ui.toolBar->addAction( QIcon(":/SharedPainter/Resources/bg_clear.png"), "Clear Background", this, SLOT(actionClearBG()) );
		ui.toolBar->addSeparator();
		ui.toolBar->addAction( QIcon(":/SharedPainter/Resources/clear_screen.png"), "Clear Screen", this, SLOT(actionClearScreen()) );
		ui.toolBar->addSeparator();
		ui.toolBar->addAction( QIcon(":/SharedPainter/Resources/last_item.png"), " Blink Last Added Item", this, SLOT(actionLastItem()) );

		ui.toolBar->addSeparator();
		ui.toolBar->addAction( tr("Painter List"), this, SLOT(actionPainterList()) );

		toolBar_SliderPlayback_ = new QSlider(Qt::Horizontal);
		ui.toolBar->addWidget( toolBar_SliderPlayback_ );
		connect( toolBar_SliderPlayback_, SIGNAL(valueChanged(int)), this, SLOT(onPlaybackSliderValueChanged(int)) );

		toolBar_GridLine_->setCheckable( true );
		toolBar_MoveMode_->setCheckable( true );
		toolBar_PenMode_->setCheckable( true );

		toolBar_SliderPlayback_->setStyleSheet( gStyleSheet_Slider );
		toolBar_SliderPlayback_->setRange(0, 0);
		changeToobarButtonColor( toolBar_penColorButton_, canvas_->penColor() );
		changeToobarButtonColor( toolBar_bgColorButton_, QColor(Qt::white) );
	}

	// create status bar
	{
		statusBarLabel_ = new QLabel();
		joinerCountLabel_ = new QLabel();
		playbackStatusLabel_ = new QLabel();
		wroteProgressBar_ = new QProgressBar();
		networkInfoLabel_ = new QLabel();
		ui.statusBar->addPermanentWidget( joinerCountLabel_ );
		ui.statusBar->addPermanentWidget( playbackStatusLabel_, 1 );
		ui.statusBar->addPermanentWidget( wroteProgressBar_ );
		ui.statusBar->addPermanentWidget( networkInfoLabel_ );
		ui.statusBar->addPermanentWidget( statusBarLabel_ );

		setStatusBar_JoinerCnt( 1 );	// my self 
		setStatusBar_PlaybackStatus( 0, 0 );
	}
	
	// create system tray
	{
		trayIconMenu_ = new QMenu(this);
		trayIconMenu_->addAction("&Open", this, SLOT(show()));
		trayIconMenu_->addAction("&About", this, SLOT(actionAbout()));
		trayIconMenu_->addSeparator();
		trayIconMenu_->addAction("E&xit", this, SLOT(actionExit()));

		trayIcon_ = new QSystemTrayIcon(this);
		trayIcon_->setContextMenu(trayIconMenu_);

		connect(trayIcon_, SIGNAL(messageClicked()), this, SLOT(onTrayMessageClicked()));
		connect(trayIcon_, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(onTrayActivated(QSystemTrayIcon::ActivationReason)));
		trayIcon_->show();
	}

	// Setting applying
	applySetting();

	// change status to "init"
	setStatus( INIT );

	// Setting default pen
	actionPenWidth3();
	actionFreePenMode();

	// Key Hooking Timer 
	keyHookTimer_ = new QTimer(this);
	keyHookTimer_->start(20);
	connect(keyHookTimer_, SIGNAL(timeout()),this, SLOT(onTimer()));

	installEventFilter(this);

	// Title
	updateWindowTitle();

	// start server 
	SharePaintManagerPtr()->startServer();
	setStatusBar_NetworkInfo( Util::getMyIPAddress(), SharePaintManagerPtr()->acceptPort() );

	// Create PaintList window
	painterListWindow_ = new PainterListWindow( this );
}

SharedPainter::~SharedPainter()
{
	screenRecoder_.unregisterObserver(this);
	UpgradeManagerPtr()->unregisterObserver( this );
	SharePaintManagerPtr()->unregisterObserver( this );
	SharePaintManagerPtr()->close();

	hideFindingServerWindow();
	hideSyncProgressWindow();

	delete keyHookTimer_;
}

void SharedPainter::applySetting( void )
{
	SharePaintManagerPtr()->changeNickName( SettingManagerPtr()->nickName() );
	SharePaintManagerPtr()->setPaintChannel( SettingManagerPtr()->paintChannel() );
	canvas_->setSettingShowLastAddItemBorder( SettingManagerPtr()->isBlinkLastItem() );
	canvas_->setHighQualityMoveItems( SettingManagerPtr()->isHighQualityMoveItemMode() );
}


// this logic is for fixing bug that the center "Enter key" don't work.
bool SharedPainter::eventFilter(QObject *object, QEvent *event)
{
	if( event->type() == QEvent::KeyPress )
	{
		QKeyEvent *keyEvt = (QKeyEvent*)event;

		if( keyEvt->key() == 0x1000004 || keyEvt->key() == Qt::Key_Enter )
		{
			if( object == ui.editMsg )
			{
				sendChatMessage();
				return true;
			}
			else if( ui.painterView->isActiveWindow() )
				actionAddText();
		}
		else if( keyEvt->key() == Qt::Key_Left || keyEvt->key() == Qt::Key_Right )
		{
			int step = keyEvt->key() == Qt::Key_Left ? -1 : +1;

			int newValue = toolBar_SliderPlayback_->value() + step;
			if( newValue >= 0 && newValue < toolBar_SliderPlayback_->maximum() )
				toolBar_SliderPlayback_->setValue( newValue );
		}
	}

	return QMainWindow::eventFilter(object,event);
}

void SharedPainter::onTimer( void )
{
	//if(!isActiveWindow())
	//	return;
	//
	//if( Util::checkKeyPressed( 'P' ) )
	//{
	//	penModeAction_->setChecked( !penModeAction_->isChecked() );
	//	actionPenMode();	
	//}
	//else if( Util::checkKeyPressed( 'T' ) )
	//{
	//	actionAddText();
	//}
}

void SharedPainter::onPlaybackSliderValueChanged( int value )
{
	if( playbackSliderFreezingFlag_ )
		return;

	SharePaintManagerPtr()->plabackTo( value - 1);

	setStatusBar_PlaybackStatus( value, toolBar_SliderPlayback_->maximum() );

	bool playback = SharePaintManagerPtr()->isPlaybackMode();

	setEnabledPainter( !playback );
}


void SharedPainter::setEnabledPainter( bool enabled )
{
	if( enabledPainter_ == enabled )
		return;

	enabledPainter_ = enabled;

	if( enabled )
	{
		canvas_->thawAction();
		qApp->restoreOverrideCursor();
	}
	else
	{
		if( canvas_->freezeAction() )
			qApp->setOverrideCursor(QCursor(QPixmap(":/SharedPainter/Resources/draw_disabled.png")));
	}

	SharePaintManagerPtr()->setEnabled( enabled );
}


void SharedPainter::onTrayMessageClicked( void )
{
	show();
}

void SharedPainter::onTrayActivated( QSystemTrayIcon::ActivationReason reason )
{
	switch ( reason ) 
	{
	case QSystemTrayIcon::Trigger:
	case QSystemTrayIcon::DoubleClick:
		show();
		break;
	default:
		;
	}
}

void SharedPainter::onPaintViewVScrollBarChanged( int value )
{
	if( SettingManagerPtr()->isSyncWindowSize() )
	{
		if( !changeScrollPosFreezingFlag_ )
			SharePaintManagerPtr()->notifyChangeCanvasScrollPos( ui.painterView->horizontalScrollBar()->value(), value );
	}
}

void SharedPainter::onPaintViewHScrollBarChanged( int value )
{
	if( SettingManagerPtr()->isSyncWindowSize() )
	{
		if( !changeScrollPosFreezingFlag_ )
			SharePaintManagerPtr()->notifyChangeCanvasScrollPos( value, ui.painterView->verticalScrollBar()->value() );
	}
}

void SharedPainter::clickedJoinerButton( void )
{
	JoinerListWindow view( SharePaintManagerPtr()->userList(), this );
	view.exec();
}

void SharedPainter::actionAbout( void )
{
	AboutWindow wnd(this);
	wnd.exec();
}

void SharedPainter::actionExit( void )
{
	trayIcon_->hide();
	close();
}

void SharedPainter::updateLastChatTime( void )
{
	//QString str = tr("last message time : ");
	QString str = QDateTime::currentDateTime().toString();
	ui.labelLastMessageTime->setText( str );
}

void SharedPainter::addSystemMessage( const QString &msg )
{
	ADD_CHAT_VERTICAL_SPACE_CHAT_BIG();

	ui.editChat->append( "<html><div class=messageSystem>" + msg + "</div></html>" );

	lastChatUserId_ = "";
}

void SharedPainter::addMyChatMessage( const QString & userId, const QString &nickName, const QString &chatMsg )
{
	bool continuousChatFlag = false;
	if( lastChatUserId_ == userId )
		continuousChatFlag = true;

	if( ! continuousChatFlag )
	{
		ADD_CHAT_VERTICAL_SPACE_CHAT_BIG();
		ui.editChat->append( "<html><div class=nicknameMine>" + nickName + "</div></html>" );
	}
	else
		ADD_CHAT_VERTICAL_SPACE_CHAT_SMALL();

	ui.editChat->append( "<html><span class=chatMark> > </span><span class=messageMine>" + chatMsg + "</span></html>" );

	lastChatUserId_ = userId;
}

void SharedPainter::addYourChatMessage( const QString & userId, const QString &nickName, const QString &chatMsg )
{
	bool continuousChatFlag = false;
	if( lastChatUserId_ == userId )
		continuousChatFlag = true;

	if( ! continuousChatFlag )
	{
		ADD_CHAT_VERTICAL_SPACE_CHAT_BIG();
		ui.editChat->append( "<html><div class=nicknameOther>" + nickName + "</div></html>" );
	}
	else
		ADD_CHAT_VERTICAL_SPACE_CHAT_SMALL();

	ui.editChat->append( "<html><span class=chatMark> > </span><span class=messageOther>" + chatMsg + "</span></html>" );

	lastChatUserId_ = userId;

	updateLastChatTime();
}


void SharedPainter::addBroadcastChatMessage( const QString & channel, const QString & userId, const QString &nickName, const QString &chatMsg )
{
	bool continuousChatFlag = false;
	if( lastChatUserId_ == userId )
		continuousChatFlag = true;

	ADD_CHAT_VERTICAL_SPACE_CHAT_BIG();
	QString who;
	who = nickName + tr(" in \"") + channel + tr("\" channel");
	ui.editChat->append( "<html><div class=nicknameBroadcast>" + who + "</div></html>" );
	ui.editChat->append( "<html><div class=messageBroadcast>" + chatMsg + "</div></html>" );

	lastChatUserId_ = "";

	updateLastChatTime();
}

void SharedPainter::actionImportFile( void )
{
	QString path;

	path = QFileDialog::getOpenFileName( this, tr("Export to file"), "", tr("Shared Paint Data File (*.sp)") );

	importFromFile( path );
}


void SharedPainter::actionExportFile( void )
{
	std::string allData = SharePaintManagerPtr()->serializeData();

	QString path;
	path = QFileDialog::getSaveFileName( this, tr("Export to file"), "", tr("Shared Paint Data File (*.sp)") );
	if( path.isEmpty() )
		return;

	exportToFile( allData, path );
}

void SharedPainter::actionSaveImageFile( void )
{
	QString path;
	path = QFileDialog::getSaveFileName( this, tr("Save image"), "", tr("jpeg file (*.jpg)") );
	if( path.isEmpty() )
		return;

	QImage newImage( canvas_->sceneRect().toRect().size(), QImage::Format_RGB32 );
	QPainter painter( &newImage );
	canvas_->render(&painter);

	newImage.save( path );
}

void SharedPainter::actionLastItem( void )
{
	canvas_->drawLastItemBorderRect();
}

void SharedPainter::actionGridLine( void )
{
	setCheckGridLineAction( canvas_->backgroundGridLineSize() <= 0 );	// for next state..

	if( canvas_->backgroundGridLineSize() > 0 )
	{
		SharePaintManagerPtr()->setBackgroundGridLine( 0 );	// clear
	}
	else
	{
		SharePaintManagerPtr()->setBackgroundGridLine( DEFAULT_GRID_LINE_SIZE_W );	// draw
	}
}

void SharedPainter::actionBGColor( void )
{
	static QColor LAST_COLOR = Qt::white;
	QColor clr = QColorDialog::getColor(canvas_->backgroundColor(), this, tr("Background Color"));
	LAST_COLOR = clr;

	if( !clr.isValid() )
		return;

	SharePaintManagerPtr()->setBackgroundColor( clr.red(), clr.green(), clr.blue(), clr.alpha() );

	changeToobarButtonColor( toolBar_bgColorButton_, clr );
}

void SharedPainter::actionConnectServer( void )
{
	if( ! getNickNameString() )
		return;

	if( ! getPaintChannelString() )
		return;	

	QString errorMsg;

	do{
		bool ok = false;

		static QString lastAddress;

		QString addr = QInputDialog::getText( this, tr("Input relay server address"),
			tr("Address:Port"), QLineEdit::Normal, SettingManagerPtr()->relayServerAddress().c_str(), &ok);

		lastAddress = addr;

		if ( !ok )
			break;

		if ( addr.isEmpty())
		{
			errorMsg = tr("Your input addres is wrong format. (IP:PORT)");
			break;
		}

		QStringList list = addr.split(":");

		if( list.size() != 2 )
		{
			errorMsg = tr("Your input addres is wrong format. (IP:PORT)");
			break;
		}

		std::string ip = list.at(0).toStdString();
		int port = list.at(1).toInt();
	
		SettingManagerPtr()->setRelayServerAddress( addr.toStdString() );

		SharePaintManagerPtr()->requestJoinServer( ip, port, SettingManagerPtr()->paintChannel() );
		return;
	} while( false );

	if( ! errorMsg.isEmpty() )
		QMessageBox::warning(this, "", errorMsg);
}

void SharedPainter::actionConnect( void )
{
	QString errorMsg;

	do{
		bool ok = false;

		static QString lastAddress;

		QString addr = QInputDialog::getText( this, tr("Input peer address"),
			tr("Address:Port"), QLineEdit::Normal, SettingManagerPtr()->peerAddress().c_str(), &ok);

		lastAddress = addr;

		if ( !ok )
			break;

		if ( addr.isEmpty())
		{
			errorMsg = tr("Your input addres is wrong format. (IP:PORT)");
			break;
		}

		QStringList list = addr.split(":");

		if( list.size() != 2 )
		{
			errorMsg = tr("Your input addres is wrong format. (IP:PORT)");
			break;
		}

		std::string ip = list.at(0).toStdString();
		int port = list.at(1).toInt();

		// save 
		SettingManagerPtr()->setPeerAddress( addr.toStdString() );

		// start connecting
		if( ! SharePaintManagerPtr()->connectToPeer( ip, port ) )
		{
			errorMsg = tr("Could not connect to the peer.");
			break;
		}
		return;
	} while( false );

	if( ! errorMsg.isEmpty() )
		QMessageBox::warning(this, "", errorMsg);
}


void SharedPainter::actionAddText( void )
{
	TextItemDialog dlg(this);
	int res = dlg.exec();
	if( res != QDialog::Accepted )
		return;

	addTextItem( dlg.text(), dlg.font(), dlg.textColor() );
}


void SharedPainter::actionPenWidth( void )
{
	bool ok = false;

	int width = QInputDialog::getInt(this, tr("Pen Width"),
                                  tr("Width:"), canvas_->penWidth(), 0, 100, 1, &ok);

	if ( !ok )
		return;

	canvas_->setPenSetting( canvas_->penColor(), width );
}

void SharedPainter::actionPenWidth1( void )
{
	canvas_->setPenSetting( canvas_->penColor(), 1 );
}

void SharedPainter::actionPenWidth3( void )
{
	canvas_->setPenSetting( canvas_->penColor(), 3 );
}

void SharedPainter::actionPenWidth6( void )
{
	canvas_->setPenSetting( canvas_->penColor(), 6 );
}

void SharedPainter::actionPenWidth10( void )
{
	canvas_->setPenSetting( canvas_->penColor(), 10 );
}

void SharedPainter::actionPenWidth20( void )
{
	canvas_->setPenSetting( canvas_->penColor(), 20 );
}

void SharedPainter::actionPenColor( void )
{
	QColor clr = QColorDialog::getColor( canvas_->penColor(), this, tr("Pen Color") );

	if( !clr.isValid() )
		return;

	canvas_->setPenSetting( clr, canvas_->penWidth() );

	changeToobarButtonColor( toolBar_penColorButton_, clr );
}

void SharedPainter::actionFreePenMode( void )
{
	toolBar_MoveMode_->setChecked( false );
	toolBar_PenMode_->setChecked( true );

	penModeAction_->setChecked( true );
	canvas_->setFreePenMode( true );
}

void SharedPainter::actionMoveMode( void )
{
	toolBar_MoveMode_->setChecked( true );
	toolBar_PenMode_->setChecked( false );

	penModeAction_->setChecked( false );
	canvas_->setFreePenMode( false );
}


void SharedPainter::actionPenMode( void )
{
	if( penModeAction_->isChecked() )
	{
		actionFreePenMode();
	}
	else
	{
		actionMoveMode();
	}
}

void SharedPainter::actionScreenStreaming( void )
{
	bool senderFlag = true;
	bool status = false;
	screenRecoder_.setWindowId( ui.painterView->winId() );

	if( screenRecoder_.isStreaming() ) 
	{
		screenRecoder_.stopStream();
		status = false;
	}
	else
	{
		if( screenRecoder_.isPlaying() )
		{
			senderFlag = false;
			status = false;
			screenRecoder_.stopStream();
		}
		else
		{
			USER_LIST list = SharePaintManagerPtr()->userList();
			for( size_t i = 0 ; i < list.size(); i++ )
			{
				if(list[i]->isScreenStreaming() && list[i]->isMyself() == false) 
				{
					QString message = tr("Now <b>'%1'</b> is streaming screen. before that user stop streaming, you cannot start streaming");
					message = message.arg( Util::toStringFromUtf8(list[i]->nickName()));
					QMessageBox::warning( this, "", message );
					return;
				}
			}

			screenRecoder_.startStreamSend();
			status = true;
		}
	}

	SharePaintManagerPtr()->notifyChangeShowScreenStream( senderFlag, status );
}

void SharedPainter::actionScreenRecord( void )
{
	bool status = false;
	screenRecoder_.setWindowId( ui.painterView->winId() );

	if( screenRecoder_.isRecording() )
	{
		screenRecoder_.stopRecord();
		status = false;
	}
	else
	{
		screenRecoder_.startRecord();
		status = true;
	}

	SharePaintManagerPtr()->notifyScreenRecordingStatus( status );
	updateRecordingStatus();
}

void SharedPainter::actionScreenShot( void )
{
	screenShotMode_ = true;
	orgPos_ = pos();
	move(DEFAULT_HIDE_POS_X, DEFAULT_HIDE_POS_Y);
}


void SharedPainter::actionClearBG( void )
{
	SharePaintManagerPtr()->clearBackground();
}

void SharedPainter::actionClearScreen( void )
{
	int res = QMessageBox::question( this, "", tr("All items and playback data will be lost and can not be rolled back.\nWould you like to proceed?"), QMessageBox::Ok|QMessageBox::Cancel);
	if( res != QMessageBox::Ok )
	{
		return;
	}

	SharePaintManagerPtr()->clearScreen();
}

void SharedPainter::actionUndo( void )
{
	SharePaintManagerPtr()->undoCommand();
}

void SharedPainter::actionRedo( void )
{
	SharePaintManagerPtr()->redoCommand();
}

void SharedPainter::actionCloseConnection( void )
{
	SharePaintManagerPtr()->closeSession();
}

void SharedPainter::actionPreferences( void )
{
	PreferencesDialog dlg(this);
	int res = dlg.exec();

	if( QDialog::Accepted == res )
	{
		applySetting();
	}
}

void SharedPainter::actionPainterList( void )
{
	painterListWindow_->show();
}

void SharedPainter::actionBroadcastTextMessage( void )
{
	bool ok;
	QString msg = QInputDialog::getText(this, tr("Broadcast Text Message"), tr("Message:"), QLineEdit::Normal, "", &ok);
	if( ! ok )
		return;

	SharePaintManagerPtr()->sendBroadCastTextMessage( SettingManagerPtr()->paintChannel(), Util::toUtf8StdString( msg ) );
}

void SharedPainter::actionNickName( void )
{
	getNickNameString( true );
}

void SharedPainter::actionPaintChannel( void )
{
	getPaintChannelString( true );
}

void SharedPainter::actionFindingServer( void )
{
	if( ! getNickNameString() )
		return;

	if( ! getPaintChannelString() )
		return;

	if( ! SharePaintManagerPtr()->isFindingServerMode() )
	{
		if( SharePaintManagerPtr()->startFindingServer() )
		{
			showFindingServerWindow();
		}
	}
	else
	{
		SharePaintManagerPtr()->stopFindingServer();
	}
}

void SharedPainter::actionClipboardPaste( void )
{
	const QClipboard *clipboard = QApplication::clipboard();
	const QMimeData *mimeData = clipboard->mimeData();

	 if (mimeData->hasImage()) 
	 {
		 int w = 0;
		 int h = 0;
		 boost::shared_ptr<CImageItem> item = boost::shared_ptr<CImageItem>(new CImageItem());
		 QPixmap pixmap( qvariant_cast<QPixmap>(mimeData->imageData()) );
		 item->setPixmap( pixmap );
		 item->setMyItem();

		 w = pixmap.width();
		 h = pixmap.height();

		 QPointF pos = Util::calculateNewItemPos( canvas_->sceneRect().width(), canvas_->sceneRect().height(), 
			 ui.painterView->mapFromGlobal(QCursor::pos()).x(),
			 ui.painterView->mapFromGlobal(QCursor::pos()).y(),
			 w, h );

		item->setPos( pos.x(), pos.y() );
		requestAddItem( item );
     } 
	 else if (mimeData->hasText())
	 {
		 QFont f(tr("Gulim"));
		 f.setPixelSize(10);
		 addTextItem( mimeData->text(), f, Util::getComplementaryColor( canvas_->backgroundColor() ) );
	 }
}


void SharedPainter::importFromFile( const QString & path )
{
	if( path.isEmpty() )
		return;

	QFile f(path);
	if( !f.open( QIODevice::ReadOnly ) )
	{
		QMessageBox::warning( this, "", tr("cannot open file.") );
		return;
	}

	QByteArray byteArray;
	byteArray = f.readAll();

	SharePaintManagerPtr()->clearScreen( true );
	if( ! SharePaintManagerPtr()->deserializeData( byteArray.data(), byteArray.size() ) )
	{
		QMessageBox::critical( this, "", tr("cannot import this file. or this file is not compatible with this version.") );
	}

	modifiedFlag_ = false;
}


void SharedPainter::exportToFile( const std::string &data, const QString & path )
{
	QFile f(path);
	if( !f.open( QIODevice::WriteOnly ) )
	{
		QMessageBox::warning( this, "", tr("cannot open file.") );
		return;
	}

	QDataStream out(&f);
	int ret = out.writeRawData( data.c_str(), data.size() );
	if( ret != (int)data.size() )
	{
		QMessageBox::warning( this, "", tr("failed to save.") );
		return;
	}
}

void SharedPainter::autoExportToFile( void )
{
	QString autoPath = qApp->applicationDirPath() + QDir::separator() + DEFAULT_AUTO_SAVE_FILE_PATH + QDir::separator();
	QDir dir( autoPath );
	if ( !dir.exists() )
		dir.mkpath( autoPath );

	autoPath += DEFAULT_AUTO_SAVE_FILE_NAME_PREFIX;

	if( exitFlag_ )
	{
		autoPath += "last.sp";
	}
	else
	{
		if( !SettingManagerPtr()->isAutoSaveData() || !modifiedFlag_ )
			return;


		autoPath += QDateTime::currentDateTime().toString( "yyMMddhhmmss");
		autoPath += ".sp";
	}

	qDebug() << "autoExportToFile" << autoPath;

	std::string allData = SharePaintManagerPtr()->serializeData();
	exportToFile( allData, autoPath );

	SettingManagerPtr()->setLastAutoSavePath( Util::toUtf8StdString(autoPath) );
}

void SharedPainter::requestAddItem( boost::shared_ptr<CPaintItem> item )
{
	item->setOwner( SharePaintManagerPtr()->myId() );

	SharePaintManagerPtr()->addPaintItem( item );
}

void SharedPainter::updateWindowTitle( void )
{
	// Title
	QString newTitle = PROGRAME_TEXT;
	newTitle += " Ver ";
	newTitle += VERSION_TEXT;
	newTitle += " (";
	newTitle += PROTOCOL_VERSION_TEXT;
	newTitle += ")";
	newTitle += ", ";
	newTitle += Util::toStringFromUtf8(SettingManagerPtr()->nickName());
	newTitle += " - Channel : ";
	if( SettingManagerPtr()->paintChannel().empty() == false )
		newTitle += Util::toStringFromUtf8(SettingManagerPtr()->paintChannel());
	else
		newTitle += tr("<Empty>");

	setWindowTitle( newTitle );
}

void SharedPainter::sendChatMessage( void )
{
	if( ! getNickNameString() )
		return;

	QString plainText = ui.editMsg->toPlainText().trimmed();
	std::string msg = Util::toUtf8StdString( plainText );

	SharePaintManagerPtr()->sendChatMessage( msg );

	ui.editMsg->setText( "" );
}

void SharedPainter::setCheckGridLineAction( bool checked )
{
	toolBar_GridLine_->setChecked( checked );
	gridLineAction_->setChecked( checked );
}

bool SharedPainter::getNickNameString( bool force )
{
	if( !force && SettingManagerPtr()->nickName().empty() == false )	// already setting
		return true;

	if( !force )
	{
		QMessageBox::warning(this, "", "Your need to set your nickname.");
	}

	bool ok;
	QString nick = QInputDialog::getText(this, tr("Nick Name"), tr("Name: any string")
		, QLineEdit::Normal, Util::toStringFromUtf8(SettingManagerPtr()->nickName()), &ok);

	if( ! ok )
	{
		if( exitFlag_ || _exit_flag )
			return false;

		if( SettingManagerPtr()->nickName().empty() )
			return getNickNameString( force );
		return true;
	}

	if( nick.isEmpty() )
	{
		QMessageBox::warning(this, "", "Invalid channel string.");
		return getNickNameString(force);
	}

	SettingManagerPtr()->setNickName( Util::toUtf8StdString(nick) );

	SharePaintManagerPtr()->changeNickName( SettingManagerPtr()->nickName() );

	updateWindowTitle();
	return true;
}

bool SharedPainter::getPaintChannelString( bool force )
{
	if( !force && SettingManagerPtr()->paintChannel().empty() == false )	// already setting
		return true;

	if( !force )
	{
		QMessageBox::warning(this, "", "Your need to set a channel first.");
	}

	bool ok;
	QString channel = QInputDialog::getText(this, tr("Paint Channel"), tr("Channel: any string"), QLineEdit::Normal, SettingManagerPtr()->paintChannel().c_str(), &ok);
	if( ! ok )
	{
		if( exitFlag_ || _exit_flag )
			return false;

		if( SettingManagerPtr()->paintChannel().empty() )
			return getPaintChannelString( force );
		return true;
	}

	if( channel.isEmpty() )
	{
		QMessageBox::warning(this, "", "Invalid channel string.");
		return getPaintChannelString( force );
	}

	std::string prevChannel = SettingManagerPtr()->paintChannel();

	bool reconnectFlag = false;
	if( prevChannel != "" && prevChannel != channel.toStdString() )
	{
		if( SharePaintManagerPtr()->isConnected() || SharePaintManagerPtr()->isConnecting() )
		{
			QString msg;
			if( SharePaintManagerPtr()->isRelayServerMode() )
			{
				msg = tr("If the channel is changed, your connection will be lost.\nAre you sure to change the channel and reconnect?");
				reconnectFlag = true;
			}
			else
			{
				msg = tr("If the channel is changed, your connection will be lost.\nAre you sure to change the channel?");
			}

			int res = QMessageBox::question( this, "", msg, QMessageBox::Ok | QMessageBox::Cancel);
			if( res != QMessageBox::Ok )
			{
				return false;
			}

			SharePaintManagerPtr()->closeSession();
		}
	}

	SettingManagerPtr()->setPaintChannel( channel.toStdString() );
	SharePaintManagerPtr()->setPaintChannel( SettingManagerPtr()->paintChannel() );

	if( reconnectFlag )
	{
		SharePaintManagerPtr()->reconnect();
	}

	updateWindowTitle();
	return true;
}

void SharedPainter::keyPressEvent ( QKeyEvent * event )  
{ 
	QWidget::keyPressEvent(event); 
} 

void SharedPainter::onAppSafeStarted( void )
{
	// must be set..
	getNickNameString();

	// automatically relay server connect
	bool connectFlag = false;
	if( SettingManagerPtr()->isRelayServerConnectOnStarting() )
	{
		QString address( SettingManagerPtr()->relayServerAddress().c_str() );
		QStringList list = address.split(":");
		if( list.size() == 2 )
		{
			std::string ip = list.at(0).toStdString();
			int port = list.at(1).toInt();
			SharePaintManagerPtr()->requestJoinServer( ip, port, SettingManagerPtr()->paintChannel() );
			connectFlag = true;
		}
	}

	if( false == connectFlag )
	{
		// load last auto saved file
		importFromFile( Util::toStringFromUtf8(SettingManagerPtr()->lastAutoSavePath()) );
	}

}

void SharedPainter::showEvent( QShowEvent * evt )
{
	int w = ui.painterView->width();
	int h = ui.painterView->height();
	canvas_->setSceneRect(0, 0, w, h);

	static bool firstShow = true;
	if( firstShow )
	{
		QTimer::singleShot( 100, this, SLOT(onAppSafeStarted()) );

		QList<int> sz;
		sz.push_back( DEFAULT_INITIAL_CHATWINDOW_SIZE );
		sz.push_back( width() - DEFAULT_INITIAL_CHATWINDOW_SIZE );
		ui.splitter->setSizes(sz);
		firstShow = false;

		w = ui.painterView->width();
		h = ui.painterView->height();
		canvas_->setSceneRect(0, 0, w, h);

		SharePaintManagerPtr()->notifyResizingCanvas( w, h );
	}
}


void SharedPainter::closeEvent( QCloseEvent *evt )
{
#ifdef Q_WS_MAC 
	// MAC OS not support trayicon. so we just exit this app.
#else
	if (trayIcon_->isVisible()) {
		QMessageBox::information(this, tr("Systray"),
			tr("The program will keep running in the "
			"system tray. To terminate the program, "
			"choose <b>Exit</b> in the context menu "
			"of the system tray entry."));
		hide();
		evt->ignore();
		return;
	}
#endif

	exitFlag_ = true;

	if( painterListWindow_ )
	{
		delete painterListWindow_;
		painterListWindow_ = NULL;
	}

	SharePaintManagerPtr()->clearScreen( false );
	SettingManagerPtr()->save();

	QMainWindow::closeEvent( evt );
}
 

void SharedPainter::moveEvent( QMoveEvent * evt )
{
	// Screen Shot Action!
	if( screenShotMode_ )
	{
		if( evt->pos().x() == DEFAULT_HIDE_POS_X && evt->pos().y() == DEFAULT_HIDE_POS_Y )
		{
			// Screen Shot!
			QPixmap pixmap = QPixmap::grabWindow(QApplication::desktop()->winId());

			// Create Backgound Image Item
			boost::shared_ptr<CBackgroundImageItem> image = boost::shared_ptr<CBackgroundImageItem>( new CBackgroundImageItem );
			image->setPixmap( pixmap );
			image->setOwner( SharePaintManagerPtr()->myId() );
			image->setItemId( 0 );

			// Send to peers
			SharePaintManagerPtr()->sendBackgroundImage( image );

			// Restore original postion..
			move(orgPos_);
			screenShotMode_ = false;
		}
	}
}

void SharedPainter::resizeEvent( QResizeEvent *evt )
{
	int w = ui.painterView->width();
	int h = ui.painterView->height();
	int sw = canvas_->sceneRect().width();
	int sh = canvas_->sceneRect().height();

	if( w > sw )
		sw = w;
	if( h > sh )
		sh = h;
	canvas_->setSceneRect(0, 0, sw, sh);
	SharePaintManagerPtr()->notifyResizingCanvas( sw, sh );

	if( SettingManagerPtr()->isSyncWindowSize() )
	{
		if( !resizeFreezingFlag_ )
			SharePaintManagerPtr()->notifyResizingMainWindow( width(), height() );
	}

	QMainWindow::resizeEvent(evt);
}

void SharedPainter::splitterMoved( int pos, int index )
{
	if( SettingManagerPtr()->isSyncWindowSize() )
	{
		if( !resizeSplitterFreezingFlag_ )
		{
			std::vector<int> vec;
			for (int i = 0; i < ui.splitter->sizes().size(); ++i)
				vec.push_back( ui.splitter->sizes().at(i) );

			SharePaintManagerPtr()->notifyResizingWindowSplitter( vec );
		}
	}

	bool prevFlag = resizeFreezingFlag_;
	resizeFreezingFlag_ = true;
	resizeEvent( NULL );
	resizeFreezingFlag_ = prevFlag;
}

void SharedPainter::onICanvasViewEvent_MoveItem( CSharedPainterScene *view, boost::shared_ptr< CPaintItem > item )
{
	SharePaintManagerPtr()->movePaintItem( item );
}

void SharedPainter::onICanvasViewEvent_DrawItem( CSharedPainterScene *view, boost::shared_ptr<CPaintItem> item  )
{
	requestAddItem( item );
}

void SharedPainter::onICanvasViewEvent_UpdateItem( CSharedPainterScene *view, boost::shared_ptr<CPaintItem> item )
{
	SharePaintManagerPtr()->updatePaintItem( item );
}

void SharedPainter::onICanvasViewEvent_RemoveItem( CSharedPainterScene *view, boost::shared_ptr<CPaintItem> item )
{
	SharePaintManagerPtr()->removePaintItem( item );
}

QString SharedPainter::onICanvasViewEvent_GetToolTipText( CSharedPainterScene *view, boost::shared_ptr<CPaintItem> item )
{
	boost::shared_ptr<CPaintUser> user = SharePaintManagerPtr()->findHistoryUser( item->owner() );
	if( user )
		return Util::toStringFromUtf8(user->nickName());
	return "";
}
