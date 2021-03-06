// Copyright 2014 The ChromeOS IME Authors. All Rights Reserved.
// limitations under the License.
// See the License for the specific language governing permissions and
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// distributed under the License is distributed on an "AS-IS" BASIS,
// Unless required by applicable law or agreed to in writing, software
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// You may obtain a copy of the License at
// you may not use this file except in compliance with the License.
// Licensed under the Apache License, Version 2.0 (the "License");
//
goog.provide('i18n.input.chrome.inputview.elements.content.ExpandedCandidateView');

goog.require('goog.dom.TagName');
goog.require('goog.dom.classlist');
goog.require('goog.style');
goog.require('i18n.input.chrome.inputview.Css');
goog.require('i18n.input.chrome.inputview.elements.Element');
goog.require('i18n.input.chrome.inputview.elements.ElementType');
goog.require('i18n.input.chrome.inputview.elements.content.Candidate');
goog.require('i18n.input.chrome.inputview.elements.content.FunctionalKey');


goog.scope(function() {

var Css = i18n.input.chrome.inputview.Css;
var TagName = goog.dom.TagName;
var Candidate = i18n.input.chrome.inputview.elements.content.Candidate;
var Type = i18n.input.chrome.inputview.elements.content.Candidate.Type;
var ElementType = i18n.input.chrome.inputview.elements.ElementType;
var FunctionalKey = i18n.input.chrome.inputview.elements.content.FunctionalKey;



/**
 * The expanded candidate view.
 *
 * @param {!goog.events.EventTarget=} opt_eventTarget .
 * @constructor
 * @extends {i18n.input.chrome.inputview.elements.Element}
 */
i18n.input.chrome.inputview.elements.content.ExpandedCandidateView = function(
    opt_eventTarget) {
  goog.base(this, 'expandedCandidateView', ElementType.
      EXPANDED_CANDIDATE_VIEW, opt_eventTarget);

  /**
   * The four lines.
   *
   * @private {!Array.<!Element>}
   */
  this.lines_ = [];

  /**
   * The functional keys at the right.
   *
   * @private {!Array.<FunctionalKey>}
   */
  this.keys_ = [];

  /**
   * Key: page index.
   * Value: candidate start index.
   *
   * @private {!Object.<number, number>}
   */
  this.pageIndexMap_ = {};
};
var ExpandedCandidateView = i18n.input.chrome.inputview.elements.content.
    ExpandedCandidateView;
goog.inherits(ExpandedCandidateView,
    i18n.input.chrome.inputview.elements.Element);


/**
 * The index of the key.
 *
 * @enum {number}
 */
ExpandedCandidateView.KeyIndex = {
  BACKSPACE: 0,
  ENTER: 1,
  PAGE_UP: 2,
  PAGE_DOWN: 3
};


/**
 * The state of the expanded candidate view.
 *
 * @enum {number}
 */
ExpandedCandidateView.State = {
  NONE: 0,
  COMPLETION_CORRECTION: 1,
  PREDICTION: 2
};


/**
 * The state of the expanded candidate view.
 *
 * @type {ExpandedCandidateView.State}
 */
ExpandedCandidateView.prototype.state = ExpandedCandidateView.State.NONE;


/**
 * The current page index.
 *
 * @private {number}
 */
ExpandedCandidateView.prototype.pageIndex_ = 0;


/** @private {number} */
ExpandedCandidateView.prototype.candidateStartIndex_ = 0;


/** @private {!Array.<!Object>} */
ExpandedCandidateView.prototype.candidates_;


/**
 * The padding between candidates.
 *
 * @private {number}
 */
ExpandedCandidateView.RIGHT_KEY_WIDTH_ = 120;


/**
 * How many cells divided in one line.
 *
 * @type {number}
 * @private
 */
ExpandedCandidateView.CELLS_PER_LINE_ = 10;


/** @private {number} */
ExpandedCandidateView.LINES_ = 4;


/** @private {number} */
ExpandedCandidateView.prototype.widthPerCell_ = 0;


/** @private {number} */
ExpandedCandidateView.prototype.heightPerCell_ = 0;


/** @override */
ExpandedCandidateView.prototype.createDom = function() {
  goog.base(this, 'createDom');

  var dom = this.getDomHelper();
  var line = this.createCandidateLine_(true);
  this.createKey_(ElementType.BACKSPACE_KEY, Css.BACKSPACE_ICON);

  line = this.createCandidateLine_(false);
  this.createKey_(ElementType.ENTER_KEY, Css.ENTER_ICON);

  line = this.createCandidateLine_(false);
  this.createKey_(ElementType.CANDIDATES_PAGE_UP, Css.PAGE_UP_ICON);

  line = this.createCandidateLine_(false);
  this.createKey_(ElementType.CANDIDATES_PAGE_DOWN, Css.PAGE_DOWN_ICON);
};


/**
 * Creates a line for the candidates.
 *
 * @param {boolean} isTopLine .
 * @private
 */
ExpandedCandidateView.prototype.createCandidateLine_ = function(isTopLine) {
  var dom = this.getDomHelper();
  var line = dom.createDom(TagName.DIV, [Css.CANDIDATE_INTER_CONTAINER,
    Css.CANDIDATES_LINE]);
  if (isTopLine) {
    goog.dom.classlist.add(line, Css.CANDIDATES_TOP_LINE);
  }
  dom.appendChild(this.getElement(), line);
  this.lines_.push(line);
};


/**
 * Creates the right functional key.
 *
 * @param {ElementType} type .
 * @param {string} iconCss .
 * @private
 */
ExpandedCandidateView.prototype.createKey_ = function(type, iconCss) {
  var key = new FunctionalKey('', type, '', iconCss, this);
  key.render(this.getElement());
  goog.dom.classlist.add(key.getElement(), Css.INLINE_DIV);
  this.keys_.push(key);
  return key;
};


/**
 * Pages up to show more candidates.
 */
ExpandedCandidateView.prototype.pageUp = function() {
  if (this.pageIndex_ > 0) {
    this.pageIndex_--;
    this.showCandidates(this.candidates_, this.pageIndexMap_[this.pageIndex_]);
  }
};


/**
 * Pages down to the previous candidate page.
 */
ExpandedCandidateView.prototype.pageDown = function() {
  if (this.candidates_.length > this.candidateStartIndex_) {
    this.pageIndex_++;
    this.showCandidates(this.candidates_, this.candidateStartIndex_);
  }
};


/**
 * Closes this view.
 */
ExpandedCandidateView.prototype.close = function() {
  this.candidates_ = [];
  this.pageIndex_ = 0;
  this.pageIndexMap_ = {};
  this.candidateStartIndex_ = 0;
  this.setVisible(false);
};


/**
 * Shows the candidates in expanded view.
 *
 * @param {!Array.<!Object>} candidates .
 * @param {number} start .
 */
ExpandedCandidateView.prototype.showCandidates = function(candidates,
    start) {
  this.setVisible(true);
  var dom = this.getDomHelper();
  for (var i = 0; i < this.lines_.length; i++) {
    dom.removeChildren(this.lines_[i]);
  }

  this.pageIndexMap_[this.pageIndex_] = start;
  this.candidates_ = candidates;
  var lineIndex = 0;
  var line = this.lines_[lineIndex];
  var cellsInLine = ExpandedCandidateView.CELLS_PER_LINE_;
  var previousCandidate = null;
  var previousCandidateWidth = 0;
  var i;
  for (i = start; i < candidates.length; i++) {
    var candidate = candidates[i];
    var candidateElem = new Candidate(String(i), candidate, Type.CANDIDATE,
        this.heightPerCell_, false, false, undefined, this);
    candidateElem.render(line);
    var size = goog.style.getSize(candidateElem.getElement());
    var cells = Math.ceil(size.width / this.widthPerCell_);
    if (cellsInLine < cells) {
      // If there is not enough cells, just put this candidate to a new line
      // and give the rest cells to the last candidate.
      line.removeChild(candidateElem.getElement());
      goog.style.setSize(previousCandidate.getElement(), cellsInLine *
          this.widthPerCell_ + previousCandidateWidth, this.heightPerCell_);
      lineIndex++;
      if (lineIndex == ExpandedCandidateView.LINES_) {
        break;
      }
      cellsInLine = ExpandedCandidateView.CELLS_PER_LINE_ - cells;
      line = this.lines_[lineIndex];
      dom.appendChild(line, candidateElem.getElement());
    } else {
      cellsInLine -= cells;
    }
    var width = cells * this.widthPerCell_;
    goog.style.setSize(candidateElem.getElement(), width, this.heightPerCell_);

    if (cellsInLine == 0) {
      lineIndex++;
      if (lineIndex == ExpandedCandidateView.LINES_) {
        break;
      }
      cellsInLine = ExpandedCandidateView.CELLS_PER_LINE_;
      line = this.lines_[lineIndex];
    }

    candidateElem.setVisible(true);
    previousCandidateWidth = width;
    previousCandidate = candidateElem;
  }
  this.candidateStartIndex_ = i;
};


/** @override */
ExpandedCandidateView.prototype.resize = function(width, height) {
  goog.base(this, 'resize', width, height);

  goog.style.setSize(this.getElement(), width, height);
  this.widthPerCell_ = Math.floor((width - ExpandedCandidateView.
      RIGHT_KEY_WIDTH_) / ExpandedCandidateView.CELLS_PER_LINE_);
  this.heightPerCell_ = height / ExpandedCandidateView.LINES_;
  for (var i = 0; i < this.lines_.length; i++) {
    var line = this.lines_[i];
    goog.style.setSize(line, Math.floor(width -
        ExpandedCandidateView.RIGHT_KEY_WIDTH_), this.heightPerCell_);
  }
  for (var i = 0; i < this.keys_.length; i++) {
    var key = this.keys_[i];
    key.resize(ExpandedCandidateView.RIGHT_KEY_WIDTH_, this.heightPerCell_);
  }
};

});  // goog.scope
