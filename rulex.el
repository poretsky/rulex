;;; rulex.el --- editing rulex dictionaries

;; Copyright (C) 2003 by Dmitri V. Paduchikh

;; Author: Dmitry Paduchikh <paduch@imm.uran.ru>
;; Keywords: tools, wp

;; This file is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.

;; This file is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GNU Emacs; see the file COPYING.  If not, write to
;; the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
;; Boston, MA 02111-1307, USA.

;;; Commentary:

;;

;;; Code:

(require 'cl)
(require 'emacspeak-sounds)
(require 'emacspeak-speak)

;;;; rulex-mode

(define-derived-mode rlex-mode nil "Russian-Lex"
  "Major mode for editing Russian pronunciation dictionaries.
\\{rlex-mode-map}"
  (activate-input-method default-input-method))

(define-key rlex-mode-map (kbd "M-e") 'rlex-edit-prod)

;;;; Word combinations

(defvar rlex-prod-map (make-sparse-keymap))

(define-key rlex-prod-map (kbd "C-c C-s") 'rlex-insert-prod)
(define-key rlex-prod-map (kbd "C-c C-c") 'rlex-insert-prod/quit)

(defvar rlex-prod-buffer nil
  "Buffer where to insert words afterwards.")

(defun rlex-edit-prod (arg)
  "Switch to the buffer for editing word productions.
Create one if it doesn't already exist.

Copy word at the point into the buffer for editing. When interactive
prefix ARG is given leave the buffer contents unchanged.

When done press \\<rlex-prod-map>\\[rlex-insert-prod/quit] or \
\\[rlex-insert-prod] to insert resulting words."
  (interactive "*P")
  (let ((w (word-at-point))
	(buf (current-buffer)))
    (switch-to-buffer (get-buffer-create "*Word Prod*"))
    (unless arg
      (erase-buffer)
      (and w (insert w))
      (goto-char (point-min))
      (set-buffer-modified-p nil))
    (make-local-variable 'rlex-prod-buffer)
    (setq rlex-prod-buffer buf))
  (use-local-map rlex-prod-map)
  (activate-input-method default-input-method)
  (emacspeak-speak-mode-line))

(defun rlex-combine-prod (prod)
  (if (null prod)
      (list "")
    (let ((res '())
	  (strings (rlex-combine-prod (cdr prod))))
      (dolist (s (car prod) res)
	(if (string= s ".") (setq s ""))
	(setq res
	      (nconc
	       (mapcar (lambda (ss) (concat s ss)) strings)
	       res))))))

(defun rlex-canonical-word (w)
  (if (zerop (length w)) w
    (when (find ?* w)
      (setq w (apply #'concat (split-string w "[+=]")))
      (setq w (nsubstitute ?+ ?* w)))
    (let* ((lastp (memq (aref w (1- (length w)))
			'(?+ ?=)))
	   (parts (split-string w "[+=]"))
	   (tail (last parts)))
      (setcar tail
	      (cond (lastp (concat (car tail) "+"))
		    ((> (length parts) 1)
		     (concat "+" (car tail)))
		    (t (car tail))))
      (setq w (apply #'concat parts)))
    (let* ((pos (position ?+ w))
	   (char (and pos (> pos 0) (aref w (1- pos)))))
      (when pos				; stressed word
	(setq w (nsubstitute ?\xe55 ?\xe71 w))
	(setq w (nsubstitute ?\xe35 ?\xe21 w))
	(when char (aset w (1- pos) char))))
    w))

(defun rlex-demangle-word (w)
  (setq w (apply #'concat (split-string w "[*+=]")))
  (setq w (nsubstitute ?\xe55 ?\xe71 w))
  (setq w (nsubstitute ?\xe35 ?\xe21 w))
  w)


(defun rlex-parse-prod ()
  (sort
   (mapcar
    (lambda (w)
      (cons (rlex-demangle-word w)
	      (rlex-canonical-word w)))
    (rlex-combine-prod
     (mapcar (lambda (s)
	       (delete-duplicates (split-string s)
				  :test #'string=))
	     (split-string (buffer-string) "|"))))
   (lambda (x y) (string< (car x) (car y)))))


(defun rlex-insert-prod ()
  (interactive)
  (unless (buffer-live-p rlex-prod-buffer)
    (error "No buffer to insert words"))
  (let ((words (rlex-parse-prod)))
    (with-current-buffer rlex-prod-buffer
      (beginning-of-line)
      (push-mark (point) t t)
      (dolist (ww words)
	(unless (string= (car ww) "")
	  (insert (car ww) " " (cdr ww) "\n")))
      (when (interactive-p)
	(emacspeak-auditory-icon 'yank-object)
	(save-excursion
	  (goto-char (mark))
	  (emacspeak-speak-line))))))

(defun rlex-insert-prod/quit ()
  (interactive)
  (rlex-insert-prod)
  (bury-buffer)
  (when (interactive-p)
    (emacspeak-auditory-icon 'close-object)
    (emacspeak-speak-mode-line)))


(provide 'rulex)
;;; rulex.el ends here
