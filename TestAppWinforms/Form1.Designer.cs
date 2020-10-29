namespace TestAppWinforms
{
    partial class Form1
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.RecordButton = new System.Windows.Forms.Button();
            this.labelStatus = new System.Windows.Forms.Label();
            this.labelTimestamp = new System.Windows.Forms.Label();
            this.PauseButton = new System.Windows.Forms.Button();
            this.labelError = new System.Windows.Forms.Label();
            this.textBoxResult = new System.Windows.Forms.TextBox();
            this.buttonOpenDirectory = new System.Windows.Forms.Button();
            this.buttonDeleteRecordedVideos = new System.Windows.Forms.Button();
            this.buttonCopyPath = new System.Windows.Forms.Button();
            this.SuspendLayout();
            // 
            // RecordButton
            // 
            this.RecordButton.Location = new System.Drawing.Point(132, 166);
            this.RecordButton.Name = "RecordButton";
            this.RecordButton.Size = new System.Drawing.Size(75, 23);
            this.RecordButton.TabIndex = 0;
            this.RecordButton.Text = "Record";
            this.RecordButton.UseVisualStyleBackColor = true;
            this.RecordButton.Click += new System.EventHandler(this.RecordButton_Click);
            // 
            // labelStatus
            // 
            this.labelStatus.AutoSize = true;
            this.labelStatus.Font = new System.Drawing.Font("Microsoft Sans Serif", 12F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.labelStatus.Location = new System.Drawing.Point(128, 114);
            this.labelStatus.Name = "labelStatus";
            this.labelStatus.Size = new System.Drawing.Size(35, 20);
            this.labelStatus.TabIndex = 1;
            this.labelStatus.Text = "Idle";
            // 
            // labelTimestamp
            // 
            this.labelTimestamp.AutoSize = true;
            this.labelTimestamp.Font = new System.Drawing.Font("Microsoft Sans Serif", 36F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.labelTimestamp.Location = new System.Drawing.Point(59, 36);
            this.labelTimestamp.Name = "labelTimestamp";
            this.labelTimestamp.Size = new System.Drawing.Size(212, 55);
            this.labelTimestamp.TabIndex = 2;
            this.labelTimestamp.Text = "00:00:00";
            // 
            // PauseButton
            // 
            this.PauseButton.Location = new System.Drawing.Point(132, 195);
            this.PauseButton.Name = "PauseButton";
            this.PauseButton.Size = new System.Drawing.Size(75, 23);
            this.PauseButton.TabIndex = 4;
            this.PauseButton.Text = "Pause";
            this.PauseButton.UseVisualStyleBackColor = true;
            this.PauseButton.Visible = false;
            this.PauseButton.Click += new System.EventHandler(this.PauseButton_Click);
            // 
            // labelError
            // 
            this.labelError.AutoSize = true;
            this.labelError.Location = new System.Drawing.Point(132, 147);
            this.labelError.Name = "labelError";
            this.labelError.Size = new System.Drawing.Size(35, 13);
            this.labelError.TabIndex = 5;
            this.labelError.Text = "label1";
            this.labelError.Visible = false;
            // 
            // textBoxResult
            // 
            this.textBoxResult.Location = new System.Drawing.Point(13, 315);
            this.textBoxResult.Name = "textBoxResult";
            this.textBoxResult.ReadOnly = true;
            this.textBoxResult.Size = new System.Drawing.Size(329, 20);
            this.textBoxResult.TabIndex = 6;
            // 
            // buttonOpenDirectory
            // 
            this.buttonOpenDirectory.Location = new System.Drawing.Point(13, 286);
            this.buttonOpenDirectory.Name = "buttonOpenDirectory";
            this.buttonOpenDirectory.Size = new System.Drawing.Size(92, 23);
            this.buttonOpenDirectory.TabIndex = 7;
            this.buttonOpenDirectory.Text = "Open directory";
            this.buttonOpenDirectory.UseVisualStyleBackColor = true;
            this.buttonOpenDirectory.Click += new System.EventHandler(this.buttonOpenDirectory_Click);
            // 
            // buttonDeleteRecordedVideos
            // 
            this.buttonDeleteRecordedVideos.Location = new System.Drawing.Point(111, 286);
            this.buttonDeleteRecordedVideos.Name = "buttonDeleteRecordedVideos";
            this.buttonDeleteRecordedVideos.Size = new System.Drawing.Size(126, 23);
            this.buttonDeleteRecordedVideos.TabIndex = 8;
            this.buttonDeleteRecordedVideos.Text = "Delete recorded videos";
            this.buttonDeleteRecordedVideos.UseVisualStyleBackColor = true;
            this.buttonDeleteRecordedVideos.Click += new System.EventHandler(this.buttonDeleteRecordedVideos_Click);
            // 
            // buttonCopyPath
            // 
            this.buttonCopyPath.Location = new System.Drawing.Point(244, 285);
            this.buttonCopyPath.Name = "buttonCopyPath";
            this.buttonCopyPath.Size = new System.Drawing.Size(75, 23);
            this.buttonCopyPath.TabIndex = 9;
            this.buttonCopyPath.Text = "Copy path";
            this.buttonCopyPath.UseVisualStyleBackColor = true;
            this.buttonCopyPath.Click += new System.EventHandler(this.buttonCopyPath_Click);
            // 
            // Form1
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(354, 347);
            this.Controls.Add(this.buttonCopyPath);
            this.Controls.Add(this.buttonDeleteRecordedVideos);
            this.Controls.Add(this.buttonOpenDirectory);
            this.Controls.Add(this.textBoxResult);
            this.Controls.Add(this.labelError);
            this.Controls.Add(this.PauseButton);
            this.Controls.Add(this.labelTimestamp);
            this.Controls.Add(this.labelStatus);
            this.Controls.Add(this.RecordButton);
            this.Name = "Form1";
            this.Text = "Form1";
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.Button RecordButton;
        private System.Windows.Forms.Label labelStatus;
        private System.Windows.Forms.Label labelTimestamp;
        private System.Windows.Forms.Button PauseButton;
        private System.Windows.Forms.Label labelError;
        private System.Windows.Forms.TextBox textBoxResult;
        private System.Windows.Forms.Button buttonOpenDirectory;
        private System.Windows.Forms.Button buttonDeleteRecordedVideos;
        private System.Windows.Forms.Button buttonCopyPath;
    }
}

